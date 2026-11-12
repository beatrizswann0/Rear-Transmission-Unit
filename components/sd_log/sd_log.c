#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "sd_log.h"

static const char *TAG = "SD_LOG";
#define MOUNT_POINT "/sdcard"

static FILE *log_file = NULL;
static int sync_counter = 0;
static char log_path[32];
static bool sd_ready;
static bool sd_mounted;

bool sd_log_ready(void)
{
    return sd_ready;
}

static unsigned next_log_index(void)
{
    unsigned max_n = 0;
    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        unsigned n = 0;
        if (sscanf(ent->d_name, "LOG%u.BIN", &n) == 1 ||
            sscanf(ent->d_name, "LOG%u.bin", &n) == 1 ||
            sscanf(ent->d_name, "log%u.bin", &n) == 1) {
            if (n > max_n) {
                max_n = n;
            }
        }
    }
    closedir(dir);
    return max_n + 1;
}

static bool try_fopen(const char *path)
{
    log_file = fopen(path, "wb");
    if (log_file == NULL) {
        ESP_LOGE(TAG, "fopen %s failed (errno=%d %s)", path, errno, strerror(errno));
        return false;
    }
    strncpy(log_path, path, sizeof(log_path) - 1);
    log_path[sizeof(log_path) - 1] = '\0';
    ESP_LOGI(TAG, "log file: %s", log_path);
    return true;
}

static void open_boot_log_file(void)
{
    unsigned idx = next_log_index();
    if (idx < 1) {
        idx = 1;
    }
    if (idx > 99999) {
        idx = 99999;
    }

    char path[32];
    snprintf(path, sizeof(path), MOUNT_POINT "/LOG%04u.BIN", idx);
    if (try_fopen(path)) {
        return;
    }
    if (try_fopen(MOUNT_POINT "/LOG.BIN")) {
        ESP_LOGW(TAG, "fallback LOG.BIN");
        return;
    }
    ESP_LOGE(TAG, "SD mounted but not writable");
}

const char *sd_log_current_path(void)
{
    if (!sd_ready || log_path[0] == '\0') {
        return "";
    }
    return log_path;
}

void sd_log_flush(void)
{
    if (log_file != NULL) {
        fflush(log_file);
        fsync(fileno(log_file));
        sync_counter = 0;
    }
}

static bool name_is_log_bin(const char *name)
{
    if (name == NULL) {
        return false;
    }
    /* LOG + dígitos + .BIN — sem path traversal. */
    if (strncmp(name, "LOG", 3) != 0 && strncmp(name, "log", 3) != 0) {
        return false;
    }
    const char *p = name + 3;
    int digits = 0;
    while (*p && isdigit((unsigned char)*p)) {
        digits++;
        p++;
    }
    if (digits < 1 || digits > 5) {
        return false;
    }
    if (strcasecmp(p, ".BIN") != 0) {
        return false;
    }
    for (const char *c = name; *c; c++) {
        if (*c == '/' || *c == '\\') {
            return false;
        }
    }
    return true;
}

int sd_log_list_json(char *buf, size_t cap)
{
    if (buf == NULL || cap < 4) {
        return 0;
    }
    if (!sd_mounted) {
        return snprintf(buf, cap, "[]");
    }
    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        return snprintf(buf, cap, "[]");
    }
    int n = 0;
    int w = snprintf(buf + n, cap - (size_t)n, "[");
    if (w < 0) {
        closedir(dir);
        return 0;
    }
    n += w;
    bool first = true;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!name_is_log_bin(ent->d_name)) {
            continue;
        }
        char full[40];
        if (snprintf(full, sizeof(full), MOUNT_POINT "/%s", ent->d_name) >= (int)sizeof(full)) {
            continue;
        }
        struct stat st;
        uint32_t sz = 0;
        if (stat(full, &st) == 0 && st.st_size >= 0) {
            sz = (uint32_t)st.st_size;
        }
        w = snprintf(buf + n, cap - (size_t)n,
                     "%s{\"name\":\"%s\",\"size\":%lu}",
                     first ? "" : ",",
                     ent->d_name, (unsigned long)sz);
        if (w < 0 || (size_t)(n + w) >= cap) {
            break;
        }
        n += w;
        first = false;
    }
    closedir(dir);
    w = snprintf(buf + n, cap - (size_t)n, "]");
    if (w > 0 && (size_t)(n + w) < cap) {
        n += w;
    }
    return n;
}

FILE *sd_log_open_read(const char *name, uint32_t *size_out)
{
    if (size_out) {
        *size_out = 0;
    }
    if (!sd_mounted || !name_is_log_bin(name)) {
        return NULL;
    }
    sd_log_flush();
    char full[40];
    if (snprintf(full, sizeof(full), MOUNT_POINT "/%s", name) >= (int)sizeof(full)) {
        return NULL;
    }
    struct stat st;
    if (stat(full, &st) != 0 || st.st_size < 0) {
        return NULL;
    }
    FILE *f = fopen(full, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (size_out) {
        *size_out = (uint32_t)st.st_size;
    }
    return f;
}

void sd_log_init(void)
{
    sd_ready = false;
    sd_mounted = false;
    log_path[0] = '\0';
    ESP_LOGI(TAG, "SD SPI CS=%d MOSI=%d CLK=%d MISO=%d",
             (int)PIN_SD_CS, (int)PIN_SD_MOSI, (int)PIN_SD_CLK, (int)PIN_SD_MISO);

    if (PIN_SD_CS < 0) {
        ESP_LOGW(TAG, "SD CS ausente em connections.md — mount skip (HW deve definir CS)");
        return;
    }

    /* Protoboard: limpar strapping residual antes do SPI. */
    gpio_reset_pin(PIN_SD_CS);
    gpio_reset_pin(PIN_SD_MOSI);
    gpio_reset_pin(PIN_SD_MISO);
    gpio_reset_pin(PIN_SD_CLK);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card = NULL;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed (%s)", esp_err_to_name(ret));
        return;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    /* Jumpers longos: começar em 400 kHz (PROBING). */
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS;
    slot_config.host_id = SPI2_HOST;

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mount failed (%s) — checar cartao/CS%d/MISO%d/MOSI%d/CLK%d/3V3",
                 esp_err_to_name(ret),
                 (int)PIN_SD_CS, (int)PIN_SD_MISO, (int)PIN_SD_MOSI, (int)PIN_SD_CLK);
        return;
    }

    ESP_LOGI(TAG, "SD mounted");
    sd_mounted = true;
    sdmmc_card_print_info(stdout, card);
    open_boot_log_file();
    sd_ready = (log_file != NULL);
}

void sd_log_data(SD_Storage_Packet *pkt)
{
    if (log_file == NULL || pkt == NULL) {
        return;
    }
    fwrite(pkt, sizeof(SD_Storage_Packet), 1, log_file);
    sync_counter++;
    if (sync_counter >= 50) {
        fflush(log_file);
        fsync(fileno(log_file));
        sync_counter = 0;
    }
}

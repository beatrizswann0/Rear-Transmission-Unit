#include "sd_fs_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * FatFs desta RTU (FF_FS_EXFAT=0, FF_LBA64=0) monta FAT12/16/32 no setor 0
 * ou numa partição MBR. exFAT e GPT não montam: o formatador do IDF
 * recria a tabela e apaga o cartão. Antes disso a raiz e um nível de
 * subpastas são lidos. Log de run encontrado bloqueia o format.
 *
 * Nomes aceitos como run: LOG.BIN, LOGnnnn.BIN, datalog.bin,
 * datalog_*.bin e datalogNNNN.bin (o firmware grava LOG0001.BIN).
 */

#define SECTOR_SIZE          512u
#define DIR_SECTOR_LIMIT     256u
#define DIR_CLUSTER_LIMIT    32u
#define SUBDIR_LIMIT         8u

#define BPB_BytsPerSec       11
#define BPB_SecPerClus       13
#define BPB_RsvdSecCnt       14
#define BPB_NumFATs          16
#define BPB_RootEntCnt       17
#define BPB_TotSec16         19
#define BPB_FATSz16          22
#define BPB_TotSec32         32
#define BPB_FATSz32          36
#define BPB_RootClus32       44
#define BS_FilSysType32      82

#define BPB_FatOfsEx         80
#define BPB_FatSzEx          84
#define BPB_DataOfsEx        88
#define BPB_NumClusEx        92
#define BPB_RootClusEx       96
#define BPB_BytsPerSecEx     108
#define BPB_SecPerClusEx     109

#define MBR_TABLE            446
#define PTE_SIZE             16
#define PTE_SYSTEM           4
#define PTE_STLBA            8

#define GPT_SIGN             "EFI PART"
#define GPTH_PT_LBA          72
#define GPTH_PT_NUM          80
#define GPTH_PTE_SIZE        84
#define GPTE_SIZE            128
#define GPTE_FST_LBA         32

#define ET_FILEDIR           0x85
#define ET_STREAM            0xC0
#define ET_FILENAME          0xC1
#define EXFAT_NO_FAT_CHAIN   0x02
#define AM_DIR               0x10
#define AM_VOL               0x08
#define AM_LFN               0x0F

typedef enum {
    SCAN_NONE = 0,
    SCAN_FOUND,
    SCAN_FAIL,
} scan_t;

typedef struct {
    sd_sector_read_fn read;
    void *ctx;
    uint32_t sector_count;
    uint8_t *sec;
    uint8_t *fat;
    uint32_t fat_lba;
    char found[40];
} probe_t;

typedef struct {
    uint32_t fat_lba;
    uint32_t first_data;
    uint8_t spc;
} fat_geom_t;

typedef struct {
    uint32_t fat_lba;
    uint32_t heap_lba;
    uint32_t cluster_count;
    uint8_t spc;
} exfat_geom_t;

typedef struct {
    uint32_t cluster;
    uint32_t contig_sectors;
} ex_sub_t;

typedef struct {
    char name[256];
    uint8_t sum;
    bool valid;
} lfn_state_t;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void set_detail(char *detail, size_t n, const char *text)
{
    if (detail == NULL || n == 0) {
        return;
    }
    snprintf(detail, n, "%s", text);
}

static bool all_digits(const char *s)
{
    if (s[0] == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
    }
    return true;
}

bool sd_is_run_log_name(const char *name)
{
    char buf[64];
    size_t n = 0;
    char *dot;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (const char *s = name; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (n + 1 >= sizeof(buf)) {
            return false;
        }
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        buf[n++] = (char)c;
    }
    buf[n] = '\0';

    dot = strrchr(buf, '.');
    if (dot == NULL || strcmp(dot, ".bin") != 0) {
        return false;
    }
    *dot = '\0';
    if (buf[0] == '\0') {
        return false;
    }
    if (strcmp(buf, "log") == 0 || strcmp(buf, "datalog") == 0) {
        return true;
    }
    if (strncmp(buf, "log", 3) == 0 && all_digits(buf + 3)) {
        return true;
    }
    if (strncmp(buf, "datalog_", 8) == 0 && buf[8] != '\0') {
        return true;
    }
    if (strncmp(buf, "datalog", 7) == 0 && all_digits(buf + 7)) {
        return true;
    }
    return false;
}

static void remember(probe_t *p, const char *name)
{
    snprintf(p->found, sizeof(p->found), "%s", name);
}

static int load(probe_t *p, uint32_t lba, uint8_t *dst)
{
    if (lba >= p->sector_count) {
        return -1;
    }
    return p->read(p->ctx, lba, dst);
}

static bool sig_55aa(const uint8_t *s)
{
    return s[510] == 0x55 && s[511] == 0xAA;
}

static bool exfat_vbr_ok(const uint8_t *s)
{
    return sig_55aa(s) && memcmp(s + 3, "EXFAT   ", 8) == 0;
}

static bool fat32_vbr_ok(const uint8_t *s)
{
    uint8_t jump = s[0];
    if (jump != 0xEB && jump != 0xE9 && jump != 0xE8) {
        return false;
    }
    return sig_55aa(s) && memcmp(s + BS_FilSysType32, "FAT32   ", 8) == 0;
}

/* Mesma heurística de check_fs() do FatFs para FAT12/16. */
static bool fat16_vbr_ok(const uint8_t *s)
{
    uint8_t jump = s[0];
    uint16_t bps;
    uint8_t spc;

    if (jump != 0xEB && jump != 0xE9 && jump != 0xE8) {
        return false;
    }
    if (fat32_vbr_ok(s)) {
        return false;
    }
    bps = rd16(s + BPB_BytsPerSec);
    spc = s[BPB_SecPerClus];
    if (bps < 512 || bps > 4096 || (bps & (bps - 1)) != 0) {
        return false;
    }
    if (spc == 0 || (spc & (spc - 1)) != 0) {
        return false;
    }
    if (rd16(s + BPB_RsvdSecCnt) == 0) {
        return false;
    }
    if ((unsigned)s[BPB_NumFATs] - 1u > 1u) {
        return false;
    }
    if (rd16(s + BPB_RootEntCnt) == 0 || rd16(s + BPB_FATSz16) == 0) {
        return false;
    }
    if (rd16(s + BPB_TotSec16) < 4 && rd32(s + BPB_TotSec32) < 0x10000u) {
        return false;
    }
    return true;
}

static bool fat_mountable(const uint8_t *s)
{
    return fat32_vbr_ok(s) || fat16_vbr_ok(s);
}

static bool sector_blank(const uint8_t *s)
{
    uint8_t fill = s[0];
    if (fill != 0x00 && fill != 0xFF) {
        return false;
    }
    for (int i = 1; i < 512; i++) {
        if (s[i] != fill) {
            return false;
        }
    }
    return true;
}

static void lfn_clear(lfn_state_t *s)
{
    memset(s, 0, sizeof(*s));
}

static uint8_t sfn_checksum(const uint8_t *e)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) << 7) | (sum >> 1));
        sum = (uint8_t)(sum + e[i]);
    }
    return sum;
}

static void lfn_push(lfn_state_t *s, const uint8_t *e)
{
    static const int offs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint8_t seq = (uint8_t)(e[0] & 0x1Fu);
    bool last = (e[0] & 0x40u) != 0;

    if (seq < 1 || seq > 20) {
        s->valid = false;
        return;
    }
    if (last) {
        memset(s->name, 0, sizeof(s->name));
        s->valid = true;
        s->sum = e[13];
    } else if (!s->valid || e[13] != s->sum) {
        s->valid = false;
        return;
    }

    int base = (seq - 1) * 13;
    for (int i = 0; i < 13; i++) {
        uint16_t cu = (uint16_t)e[offs[i]] | ((uint16_t)e[offs[i] + 1] << 8);
        int pos = base + i;
        if (pos >= (int)sizeof(s->name) - 1) {
            s->valid = false;
            return;
        }
        if (cu == 0x0000 || cu == 0xFFFF) {
            s->name[pos] = '\0';
            break;
        }
        s->name[pos] = (cu < 128) ? (char)cu : '?';
    }
}

static void build_83(const uint8_t *e, char *out)
{
    uint8_t raw[11];
    int p = 0;

    memcpy(raw, e, 11);
    if (raw[0] == 0x05) {
        raw[0] = 0xE5;
    }
    for (int i = 0; i < 8 && raw[i] != ' '; i++) {
        out[p++] = (char)raw[i];
    }
    if (raw[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) {
            out[p++] = (char)raw[i];
        }
    }
    out[p] = '\0';
}

static bool is_dot_dir(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int fat_next(probe_t *p, uint32_t fat_lba, uint32_t cluster, uint32_t *next)
{
    uint32_t offset = cluster * 4u;
    uint32_t lba = fat_lba + offset / SECTOR_SIZE;
    uint32_t idx = offset % SECTOR_SIZE;

    if (idx + 4u > SECTOR_SIZE) {
        return -1;
    }
    if (p->fat_lba != lba) {
        if (load(p, lba, p->fat) != 0) {
            return -1;
        }
        p->fat_lba = lba;
    }
    *next = rd32(p->fat + idx) & 0x0FFFFFFFu;
    return 0;
}

static scan_t take_name(probe_t *p, const char *name)
{
    if (!sd_is_run_log_name(name)) {
        return SCAN_NONE;
    }
    remember(p, name);
    return SCAN_FOUND;
}

/* Um setor de diretório FAT. ended fica true no marcador 0x00. */
static scan_t parse_fat_sector(probe_t *p, const uint8_t *sec, bool follow_sub,
                               bool fat32, uint32_t *subs, int *nsub, bool *blocked,
                               bool *ended, lfn_state_t *lfn)
{
    int entries = (int)(SECTOR_SIZE / 32u);

    for (int i = 0; i < entries; i++) {
        const uint8_t *e = sec + (i * 32);
        char shortname[16];
        const char *picked;

        if (e[0] == 0x00) {
            *ended = true;
            return SCAN_NONE;
        }
        if (e[0] == 0xE5) {
            lfn_clear(lfn);
            continue;
        }
        if (e[11] == AM_LFN) {
            lfn_push(lfn, e);
            continue;
        }
        if ((e[11] & AM_VOL) != 0) {
            lfn_clear(lfn);
            continue;
        }

        build_83(e, shortname);
        picked = shortname;
        if (lfn->valid && sfn_checksum(e) == lfn->sum && lfn->name[0] != '\0') {
            picked = lfn->name;
        }
        if (take_name(p, picked) == SCAN_FOUND ||
            (picked != shortname && take_name(p, shortname) == SCAN_FOUND)) {
            return SCAN_FOUND;
        }

        if ((e[11] & AM_DIR) != 0 && !is_dot_dir(shortname)) {
            uint32_t cl = rd16(e + 26);
            if (fat32) {
                cl |= (uint32_t)rd16(e + 20) << 16;
            }
            if (!follow_sub || cl < 2) {
                *blocked = true;
            } else if (*nsub >= (int)SUBDIR_LIMIT) {
                *blocked = true;
            } else {
                subs[(*nsub)++] = cl;
            }
        }
        lfn_clear(lfn);
    }
    return SCAN_NONE;
}

static scan_t scan_fat32_chain(probe_t *p, const fat_geom_t *g, uint32_t cluster,
                               bool follow_sub)
{
    lfn_state_t lfn;
    uint32_t subs[SUBDIR_LIMIT];
    int nsub = 0;
    bool blocked = false;
    bool ended = false;
    bool eoc = false;
    unsigned guard = 0;
    unsigned sectors = 0;

    lfn_clear(&lfn);
    while (guard < DIR_CLUSTER_LIMIT && !ended) {
        uint32_t next;
        uint64_t base;

        if (cluster < 2 || cluster >= 0x0FFFFFF7u) {
            return SCAN_FAIL;
        }
        base = (uint64_t)g->first_data + (uint64_t)(cluster - 2u) * g->spc;
        for (uint8_t s = 0; s < g->spc && !ended; s++) {
            if (sectors >= DIR_SECTOR_LIMIT || base + s >= p->sector_count) {
                return SCAN_FAIL;
            }
            if (load(p, (uint32_t)(base + s), p->sec) != 0) {
                return SCAN_FAIL;
            }
            if (parse_fat_sector(p, p->sec, follow_sub, true, subs, &nsub,
                                 &blocked, &ended, &lfn) == SCAN_FOUND) {
                return SCAN_FOUND;
            }
            sectors++;
        }
        if (ended) {
            break;
        }
        if (fat_next(p, g->fat_lba, cluster, &next) != 0) {
            return SCAN_FAIL;
        }
        if (next >= 0x0FFFFFF8u) {
            eoc = true;
            break;
        }
        if (next < 2 || next == cluster) {
            return SCAN_FAIL;
        }
        cluster = next;
        guard++;
    }
    if (!ended && !eoc) {
        return SCAN_FAIL;
    }
    if (!follow_sub) {
        return SCAN_NONE;
    }
    if (blocked) {
        return SCAN_FAIL;
    }
    for (int i = 0; i < nsub; i++) {
        scan_t sub = scan_fat32_chain(p, g, subs[i], false);
        if (sub != SCAN_NONE) {
            return sub;
        }
    }
    return SCAN_NONE;
}

static bool fat32_geom(const uint8_t *vbr, uint32_t vol_lba, uint32_t sector_count,
                       fat_geom_t *g, uint32_t *root_cluster)
{
    uint16_t bps = rd16(vbr + BPB_BytsPerSec);
    uint8_t spc = vbr[BPB_SecPerClus];
    uint16_t reserved = rd16(vbr + BPB_RsvdSecCnt);
    uint8_t nfats = vbr[BPB_NumFATs];
    uint32_t fatsz = rd32(vbr + BPB_FATSz32);
    uint32_t root = rd32(vbr + BPB_RootClus32);
    uint64_t first;

    if (bps != SECTOR_SIZE || spc == 0 || (spc & (spc - 1)) != 0 || spc > 128) {
        return false;
    }
    if (reserved == 0 || nfats < 1 || nfats > 2 || fatsz == 0 || root < 2) {
        return false;
    }
    first = (uint64_t)vol_lba + reserved + (uint64_t)nfats * fatsz;
    if (first >= sector_count) {
        return false;
    }
    g->fat_lba = vol_lba + reserved;
    g->first_data = (uint32_t)first;
    g->spc = spc;
    *root_cluster = root;
    return true;
}

static scan_t scan_fat32(probe_t *p, uint32_t vol_lba)
{
    fat_geom_t g;
    uint32_t root = 0;

    if (load(p, vol_lba, p->sec) != 0 || !fat32_vbr_ok(p->sec)) {
        return SCAN_FAIL;
    }
    if (!fat32_geom(p->sec, vol_lba, p->sector_count, &g, &root)) {
        return SCAN_FAIL;
    }
    p->fat_lba = UINT32_MAX;
    return scan_fat32_chain(p, &g, root, true);
}

/* FAT12/16: raiz contígua. Subpasta não é seguida; se existir, a busca fica incompleta. */
static scan_t scan_fat16_root(probe_t *p, uint32_t vol_lba)
{
    uint16_t bps;
    uint16_t reserved;
    uint16_t root_ent;
    uint16_t fatsz;
    uint8_t nfats;
    uint32_t root_lba;
    uint32_t root_secs;
    lfn_state_t lfn;
    bool ended = false;
    bool blocked = false;

    if (load(p, vol_lba, p->sec) != 0) {
        return SCAN_FAIL;
    }
    bps = rd16(p->sec + BPB_BytsPerSec);
    reserved = rd16(p->sec + BPB_RsvdSecCnt);
    root_ent = rd16(p->sec + BPB_RootEntCnt);
    fatsz = rd16(p->sec + BPB_FATSz16);
    nfats = p->sec[BPB_NumFATs];
    if (bps != SECTOR_SIZE || reserved == 0 || root_ent == 0 || fatsz == 0 ||
        nfats < 1 || nfats > 2) {
        return SCAN_FAIL;
    }
    root_lba = vol_lba + reserved + (uint32_t)nfats * fatsz;
    root_secs = ((uint32_t)root_ent * 32u + (SECTOR_SIZE - 1u)) / SECTOR_SIZE;
    if (root_secs == 0 || root_secs > DIR_SECTOR_LIMIT ||
        (uint64_t)root_lba + root_secs > p->sector_count) {
        return SCAN_FAIL;
    }

    lfn_clear(&lfn);
    for (uint32_t s = 0; s < root_secs && !ended; s++) {
        if (load(p, root_lba + s, p->sec) != 0) {
            return SCAN_FAIL;
        }
        if (parse_fat_sector(p, p->sec, false, false, NULL, NULL, &blocked,
                             &ended, &lfn) == SCAN_FOUND) {
            return SCAN_FOUND;
        }
    }
    if (blocked) {
        return SCAN_FAIL;
    }
    return SCAN_NONE;
}

static int exfat_next(probe_t *p, uint32_t fat_lba, uint32_t cluster, uint32_t *next)
{
    uint32_t offset = cluster * 4u;
    uint32_t lba = fat_lba + offset / SECTOR_SIZE;
    uint32_t idx = offset % SECTOR_SIZE;

    if (p->fat_lba != lba) {
        if (load(p, lba, p->fat) != 0) {
            return -1;
        }
        p->fat_lba = lba;
    }
    *next = rd32(p->fat + idx);
    return 0;
}

typedef struct {
    int set_left;
    bool in_set;
    bool in_use;
    bool is_dir;
    bool saw_stream;
    bool no_fat;
    uint32_t cluster;
    uint64_t datalen;
    char name[256];
    int name_got;
    int name_want;
} ex_st_t;

static void ex_st_init(ex_st_t *st)
{
    memset(st, 0, sizeof(*st));
}

static void exfat_append_name(ex_st_t *st, const uint8_t *e)
{
    for (int i = 0; i < 15 && st->name_got < st->name_want && st->name_got < 255; i++) {
        uint16_t cu = rd16(e + 2 + (i * 2));
        if (cu == 0x0000) {
            break;
        }
        st->name[st->name_got++] = (cu < 128) ? (char)cu : '?';
    }
    st->name[st->name_got] = '\0';
}

static scan_t exfat_finish_set(probe_t *p, const ex_st_t *st, bool follow_sub,
                               ex_sub_t *subs, int *nsub, bool *blocked)
{
    if (!st->in_use) {
        return SCAN_NONE;
    }
    if (!st->saw_stream || st->name_got != st->name_want) {
        return SCAN_FAIL;
    }
    if (take_name(p, st->name) == SCAN_FOUND) {
        return SCAN_FOUND;
    }
    if (!st->is_dir || is_dot_dir(st->name) || st->cluster < 2) {
        return SCAN_NONE;
    }
    if (!follow_sub || *nsub >= (int)SUBDIR_LIMIT) {
        *blocked = true;
        return SCAN_NONE;
    }
    subs[*nsub].cluster = st->cluster;
    if (st->no_fat) {
        uint64_t nsec = (st->datalen + (SECTOR_SIZE - 1u)) / SECTOR_SIZE;
        if (nsec == 0 || nsec > DIR_SECTOR_LIMIT) {
            *blocked = true;
            return SCAN_NONE;
        }
        subs[*nsub].contig_sectors = (uint32_t)nsec;
    } else {
        subs[*nsub].contig_sectors = 0;
    }
    (*nsub)++;
    return SCAN_NONE;
}

static scan_t scan_exfat_entries(probe_t *p, const uint8_t *sec, bool follow_sub,
                                 ex_sub_t *subs, int *nsub, bool *blocked, bool *ended,
                                 ex_st_t *st)
{
    for (int i = 0; i < (int)(SECTOR_SIZE / 32u); i++) {
        const uint8_t *e = sec + (i * 32);
        uint8_t type = e[0];

        if (st->in_set) {
            if (type == 0x00) {
                return SCAN_FAIL;
            }
            if ((type & 0x7Fu) == (ET_STREAM & 0x7Fu)) {
                st->name_want = e[3];
                st->cluster = rd32(e + 20);
                st->no_fat = (e[1] & EXFAT_NO_FAT_CHAIN) != 0;
                st->datalen = rd64(e + 24);
                st->saw_stream = true;
                st->name_got = 0;
                st->name[0] = '\0';
            } else if ((type & 0x7Fu) == (ET_FILENAME & 0x7Fu)) {
                exfat_append_name(st, e);
            }
            st->set_left--;
            if (st->set_left == 0) {
                scan_t got = exfat_finish_set(p, st, follow_sub, subs, nsub, blocked);
                st->in_set = false;
                if (got != SCAN_NONE) {
                    return got;
                }
            }
            continue;
        }

        if (type == 0x00) {
            *ended = true;
            return SCAN_NONE;
        }
        if ((type & 0x40u) == 0 && (type & 0x1Fu) == (ET_FILEDIR & 0x1Fu)) {
            int sec_count = e[1];
            if (sec_count < 1 || sec_count > 18) {
                return SCAN_FAIL;
            }
            ex_st_init(st);
            st->in_set = true;
            st->set_left = sec_count;
            st->in_use = (type & 0x80u) != 0;
            st->is_dir = (rd16(e + 4) & AM_DIR) != 0;
        }
    }
    return SCAN_NONE;
}

static scan_t feed_exfat_sectors(probe_t *p, uint64_t start_lba, uint32_t nsec,
                                  bool follow_sub, ex_sub_t *subs, int *nsub, bool *blocked,
                                  bool *ended, ex_st_t *st, unsigned *sectors)
{
    if (nsec > DIR_SECTOR_LIMIT) {
        return SCAN_FAIL;
    }
    for (uint32_t s = 0; s < nsec && !(*ended); s++) {
        scan_t got;

        if (*sectors >= DIR_SECTOR_LIMIT || start_lba + s >= p->sector_count) {
            return SCAN_FAIL;
        }
        if (load(p, (uint32_t)(start_lba + s), p->sec) != 0) {
            return SCAN_FAIL;
        }
        got = scan_exfat_entries(p, p->sec, follow_sub, subs, nsub, blocked, ended, st);
        if (got != SCAN_NONE) {
            return got;
        }
        (*sectors)++;
    }
    return SCAN_NONE;
}

static scan_t scan_exfat_dir(probe_t *p, const exfat_geom_t *g, uint32_t cluster,
                             uint32_t contig_sectors, bool follow_sub)
{
    ex_sub_t subs[SUBDIR_LIMIT];
    ex_st_t st;
    int nsub = 0;
    bool blocked = false;
    bool ended = false;
    unsigned sectors = 0;
    scan_t got = SCAN_NONE;

    ex_st_init(&st);
    if (contig_sectors > 0) {
        uint64_t base = (uint64_t)g->heap_lba + (uint64_t)(cluster - 2u) * g->spc;
        got = feed_exfat_sectors(p, base, contig_sectors, follow_sub, subs, &nsub, &blocked,
                                 &ended, &st, &sectors);
    } else {
        unsigned guard = 0;
        while (got == SCAN_NONE && guard < DIR_CLUSTER_LIMIT && !ended) {
            uint32_t next;
            uint64_t base;

            if (cluster < 2 || cluster >= g->cluster_count + 2u) {
                return SCAN_FAIL;
            }
            base = (uint64_t)g->heap_lba + (uint64_t)(cluster - 2u) * g->spc;
            got = feed_exfat_sectors(p, base, g->spc, follow_sub, subs, &nsub, &blocked,
                                     &ended, &st, &sectors);
            if (got != SCAN_NONE || ended) {
                break;
            }
            if (exfat_next(p, g->fat_lba, cluster, &next) != 0) {
                return SCAN_FAIL;
            }
            if (next >= 0xFFFFFFF8u) {
                break;
            }
            if (next < 2 || next == cluster) {
                return SCAN_FAIL;
            }
            cluster = next;
            guard++;
        }
        if (got == SCAN_NONE && !ended && guard >= DIR_CLUSTER_LIMIT) {
            return SCAN_FAIL;
        }
    }
    if (got != SCAN_NONE) {
        return got;
    }
    if (st.in_set) {
        return SCAN_FAIL;
    }
    if (!follow_sub) {
        return SCAN_NONE;
    }
    if (blocked) {
        return SCAN_FAIL;
    }
    for (int i = 0; i < nsub; i++) {
        scan_t sub = scan_exfat_dir(p, g, subs[i].cluster, subs[i].contig_sectors, false);
        if (sub != SCAN_NONE) {
            return sub;
        }
    }
    return SCAN_NONE;
}

static scan_t scan_exfat(probe_t *p, uint32_t vol_lba)
{
    exfat_geom_t g;
    uint32_t root;
    uint32_t fat_off;
    uint32_t heap_off;
    uint32_t clusters;
    uint8_t bps_shift;
    uint8_t spc_shift;

    if (load(p, vol_lba, p->sec) != 0 || !exfat_vbr_ok(p->sec)) {
        return SCAN_FAIL;
    }
    fat_off = rd32(p->sec + BPB_FatOfsEx);
    heap_off = rd32(p->sec + BPB_DataOfsEx);
    clusters = rd32(p->sec + BPB_NumClusEx);
    root = rd32(p->sec + BPB_RootClusEx);
    bps_shift = p->sec[BPB_BytsPerSecEx];
    spc_shift = p->sec[BPB_SecPerClusEx];
    if (bps_shift != 9 || spc_shift > 8 || fat_off == 0 || heap_off <= fat_off) {
        return SCAN_FAIL;
    }
    if (clusters < 1 || root < 2 || root >= clusters + 2u) {
        return SCAN_FAIL;
    }
    if ((uint64_t)vol_lba + heap_off >= p->sector_count) {
        return SCAN_FAIL;
    }
    g.fat_lba = vol_lba + fat_off;
    g.heap_lba = vol_lba + heap_off;
    g.cluster_count = clusters;
    g.spc = (uint8_t)(1u << spc_shift);
    p->fat_lba = UINT32_MAX;
    return scan_exfat_dir(p, &g, root, 0, true);
}

static sd_probe_action_t from_scan(scan_t s, const char *fs, const probe_t *p,
                                   char *detail, size_t n)
{
    if (s == SCAN_FOUND) {
        snprintf(detail, n, "%s, %s", fs, p->found);
        return SD_PROBE_HAS_RUNS;
    }
    if (s == SCAN_FAIL) {
        snprintf(detail, n, "%s ilegivel", fs);
        return SD_PROBE_IO_ERROR;
    }
    snprintf(detail, n, "%s sem logs de run", fs);
    return SD_PROBE_FORMAT;
}

/* true quando o setor é um volume que sabemos classificar. */
static bool classify_volume(probe_t *p, uint32_t lba, bool fat_is_mountable,
                            sd_probe_action_t *action, char *detail, size_t n)
{
    if (load(p, lba, p->sec) != 0) {
        set_detail(detail, n, "leitura do volume falhou");
        *action = SD_PROBE_IO_ERROR;
        return true;
    }
    if (exfat_vbr_ok(p->sec)) {
        *action = from_scan(scan_exfat(p, lba), "exFAT", p, detail, n);
        return true;
    }
    if (fat_mountable(p->sec) && fat_is_mountable) {
        set_detail(detail, n, fat32_vbr_ok(p->sec) ? "FAT32" : "FAT");
        *action = SD_PROBE_MOUNTABLE;
        return true;
    }
    if (fat32_vbr_ok(p->sec)) {
        *action = from_scan(scan_fat32(p, lba), "FAT32", p, detail, n);
        return true;
    }
    if (fat16_vbr_ok(p->sec)) {
        *action = from_scan(scan_fat16_root(p, lba), "FAT16", p, detail, n);
        return true;
    }
    return false;
}

static bool guid_zero(const uint8_t *g)
{
    for (int i = 0; i < 16; i++) {
        if (g[i] != 0) {
            return false;
        }
    }
    return true;
}

/* Microsoft Reserved — não tem arquivo. O resto que não for FAT/exFAT bloqueia o format. */
static sd_probe_action_t worse(sd_probe_action_t cur, sd_probe_action_t add);

static bool guid_msr(const uint8_t *g)
{
    static const uint8_t msr[16] = {
        0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
        0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE
    };
    return memcmp(g, msr, 16) == 0;
}

static void note_action(sd_probe_action_t *action, char *best, size_t best_len,
                        sd_probe_action_t one, const char *one_detail)
{
    sd_probe_action_t next = worse(*action, one);
    if (best[0] == '\0' || next != *action) {
        snprintf(best, best_len, "%s", one_detail);
    }
    *action = next;
}

static sd_probe_action_t worse(sd_probe_action_t cur, sd_probe_action_t add)
{
    /* MOUNTABLE e HAS_RUNS não formatam. IO_ERROR também não.
     * FORMAT só vence se todo volume reconhecido estiver limpo. */
    if (cur == SD_PROBE_MOUNTABLE || add == SD_PROBE_MOUNTABLE) {
        return SD_PROBE_MOUNTABLE;
    }
    if (cur == SD_PROBE_HAS_RUNS || add == SD_PROBE_HAS_RUNS) {
        return SD_PROBE_HAS_RUNS;
    }
    if (cur == SD_PROBE_IO_ERROR || add == SD_PROBE_IO_ERROR) {
        return SD_PROBE_IO_ERROR;
    }
    return SD_PROBE_FORMAT;
}

static sd_probe_action_t probe_gpt(probe_t *p, char *detail, size_t n)
{
    uint64_t pt_lba;
    uint32_t nent;
    uint32_t entsz;
    bool any = false;
    sd_probe_action_t action = SD_PROBE_FORMAT;
    char best[80];

    if (load(p, 1, p->sec) != 0) {
        set_detail(detail, n, "GPT ilegivel");
        return SD_PROBE_IO_ERROR;
    }
    if (memcmp(p->sec, GPT_SIGN, 8) != 0) {
        set_detail(detail, n, "MBR protetor sem GPT");
        return SD_PROBE_IO_ERROR;
    }
    pt_lba = rd64(p->sec + GPTH_PT_LBA);
    nent = rd32(p->sec + GPTH_PT_NUM);
    entsz = rd32(p->sec + GPTH_PTE_SIZE);
    if (entsz != GPTE_SIZE || nent == 0 || nent > 128 || pt_lba == 0 ||
        pt_lba >= p->sector_count) {
        set_detail(detail, n, "cabecalho GPT invalido");
        return SD_PROBE_IO_ERROR;
    }

    best[0] = '\0';
    for (uint32_t i = 0; i < nent; i++) {
        uint64_t off = (uint64_t)i * GPTE_SIZE;
        uint64_t lba = pt_lba + off / SECTOR_SIZE;
        uint32_t idx = (uint32_t)(off % SECTOR_SIZE);
        uint64_t start;
        sd_probe_action_t one;
        char one_detail[80];
        uint8_t entry[GPTE_SIZE];

        if (lba >= p->sector_count || load(p, (uint32_t)lba, p->sec) != 0) {
            set_detail(detail, n, "tabela GPT ilegivel");
            return SD_PROBE_IO_ERROR;
        }
        memcpy(entry, p->sec + idx, GPTE_SIZE);
        if (guid_zero(entry)) {
            continue;
        }
        start = rd64(entry + GPTE_FST_LBA);
        if (start == 0) {
            continue;
        }
        if (start >= p->sector_count || (start >> 32) != 0) {
            set_detail(detail, n, "particao GPT fora do cartao");
            return SD_PROBE_IO_ERROR;
        }
        if (!classify_volume(p, (uint32_t)start, false, &one, one_detail, sizeof(one_detail))) {
            if (guid_msr(entry)) {
                continue;
            }
            set_detail(detail, n, "particao GPT desconhecida");
            return SD_PROBE_IO_ERROR;
        }
        any = true;
        note_action(&action, best, sizeof(best), one, one_detail);
        if (action == SD_PROBE_HAS_RUNS) {
            set_detail(detail, n, best);
            return action;
        }
    }
    if (!any) {
        set_detail(detail, n, "GPT sem FAT/exFAT");
        return SD_PROBE_IO_ERROR;
    }
    set_detail(detail, n, best);
    return action;
}

static bool mbr_has_ee(const uint8_t *s)
{
    for (int i = 0; i < 4; i++) {
        if (s[MBR_TABLE + i * PTE_SIZE + PTE_SYSTEM] == 0xEE) {
            return true;
        }
    }
    return false;
}

static bool mbr_has_lba(const uint8_t *s)
{
    for (int i = 0; i < 4; i++) {
        if (rd32(s + MBR_TABLE + i * PTE_SIZE + PTE_STLBA) != 0) {
            return true;
        }
    }
    return false;
}

static sd_probe_action_t probe_mbr(probe_t *p, const uint8_t *boot, char *detail, size_t n)
{
    bool any = false;
    sd_probe_action_t action = SD_PROBE_FORMAT;
    char best[80];

    best[0] = '\0';
    for (int i = 0; i < 4; i++) {
        uint32_t lba = rd32(boot + MBR_TABLE + i * PTE_SIZE + PTE_STLBA);
        sd_probe_action_t one;
        char one_detail[80];

        if (lba == 0) {
            continue;
        }
        if (lba >= p->sector_count) {
            set_detail(detail, n, "particao MBR fora do cartao");
            return SD_PROBE_IO_ERROR;
        }
        if (!classify_volume(p, lba, true, &one, one_detail, sizeof(one_detail))) {
            one = SD_PROBE_IO_ERROR;
            snprintf(one_detail, sizeof(one_detail), "particao MBR desconhecida");
        }
        any = true;
        note_action(&action, best, sizeof(best), one, one_detail);
        if (action == SD_PROBE_MOUNTABLE) {
            set_detail(detail, n, best);
            return action;
        }
    }
    if (!any) {
        set_detail(detail, n, "MBR sem particao");
        return SD_PROBE_IO_ERROR;
    }
    set_detail(detail, n, best);
    return action;
}

static sd_probe_action_t probe_orphans(probe_t *p, char *detail, size_t n)
{
    static const uint32_t offs[] = {63u, 2048u, 8192u};
    bool any = false;
    sd_probe_action_t action = SD_PROBE_FORMAT;
    char best[80];

    best[0] = '\0';
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) {
        sd_probe_action_t one;
        char one_detail[80];

        if (offs[i] >= p->sector_count) {
            continue;
        }
        if (!classify_volume(p, offs[i], false, &one, one_detail, sizeof(one_detail))) {
            continue;
        }
        any = true;
        note_action(&action, best, sizeof(best), one, one_detail);
        if (action == SD_PROBE_HAS_RUNS) {
            set_detail(detail, n, best);
            return action;
        }
    }
    if (!any) {
        return SD_PROBE_FORMAT;
    }
    set_detail(detail, n, best);
    return action;
}

sd_probe_action_t sd_fs_probe(sd_sector_read_fn read_sector, void *ctx,
                              uint32_t sector_size, uint32_t sector_count,
                              char *detail, size_t detail_len)
{
    probe_t p;
    uint8_t *boot = NULL;
    sd_probe_action_t action = SD_PROBE_IO_ERROR;

    set_detail(detail, detail_len, "inspecao incompleta");
    if (read_sector == NULL || sector_size != SECTOR_SIZE || sector_count < 1) {
        set_detail(detail, detail_len, "geometria invalida");
        return SD_PROBE_IO_ERROR;
    }

    memset(&p, 0, sizeof(p));
    p.read = read_sector;
    p.ctx = ctx;
    p.sector_count = sector_count;
    p.fat_lba = UINT32_MAX;
    p.sec = (uint8_t *)malloc(SECTOR_SIZE);
    p.fat = (uint8_t *)malloc(SECTOR_SIZE);
    boot = (uint8_t *)malloc(SECTOR_SIZE);
    if (p.sec == NULL || p.fat == NULL || boot == NULL) {
        set_detail(detail, detail_len, "sem memoria");
        goto done;
    }
    if (load(&p, 0, boot) != 0) {
        set_detail(detail, detail_len, "leitura do setor 0 falhou");
        goto done;
    }

    if (exfat_vbr_ok(boot) || fat_mountable(boot)) {
        action = SD_PROBE_FORMAT;
        if (!classify_volume(&p, 0, true, &action, detail, detail_len)) {
            set_detail(detail, detail_len, "volume do setor 0 ilegivel");
            action = SD_PROBE_IO_ERROR;
        }
        goto done;
    }
    if (sig_55aa(boot) && mbr_has_ee(boot)) {
        action = probe_gpt(&p, detail, detail_len);
        goto done;
    }
    if (sig_55aa(boot) && mbr_has_lba(boot)) {
        action = probe_mbr(&p, boot, detail, detail_len);
        goto done;
    }

    action = probe_orphans(&p, detail, detail_len);
    if (action == SD_PROBE_FORMAT &&
        (detail == NULL || strstr(detail, "sem logs") == NULL)) {
        if (sector_blank(boot)) {
            set_detail(detail, detail_len, "cartao vazio");
        } else {
            set_detail(detail, detail_len, "sem sistema de arquivos");
        }
    }

done:
    free(p.sec);
    free(p.fat);
    free(boot);
    return action;
}

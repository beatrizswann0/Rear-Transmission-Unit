#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Resultado da inspeção do cartão antes de formatar.
 *
 * MOUNTABLE: FAT que o FatFs desta RTU consegue montar (MBR ou setor 0).
 * FORMAT:    formato que não monta (exFAT, GPT, vazio) e sem log de run.
 * HAS_RUNS:  formato que não monta, mas a raiz (ou uma subpasta) tem log.
 * IO_ERROR:  não deu para ter certeza. Não formatar.
 */
typedef enum {
    SD_PROBE_IO_ERROR = 0,
    SD_PROBE_MOUNTABLE,
    SD_PROBE_FORMAT,
    SD_PROBE_HAS_RUNS,
} sd_probe_action_t;

/* 0 = ok, diferente de 0 = falha de leitura. buf recebe um setor. */
typedef int (*sd_sector_read_fn)(void *ctx, uint32_t lba, uint8_t *buf);

sd_probe_action_t sd_fs_probe(sd_sector_read_fn read_sector, void *ctx,
                              uint32_t sector_size, uint32_t sector_count,
                              char *detail, size_t detail_len);

/* LOG.BIN, LOGnnnn.BIN, datalog.bin, datalog_*.bin, datalogNNNN.bin. */
bool sd_is_run_log_name(const char *name);

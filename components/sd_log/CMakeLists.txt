idf_component_register(SRCS "sd_log.c" "sd_fs_probe.c"
                       INCLUDE_DIRS "include" "." "../../../../shared"
                       REQUIRES board fatfs sdmmc driver freertos log)

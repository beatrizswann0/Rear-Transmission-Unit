idf_component_register(SRCS "can_mgmt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board driver esp_driver_gpio freertos log)

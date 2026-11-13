idf_component_register(SRCS "auth_service.cpp" "auth_types.cpp" INCLUDE_DIRS "include" REQUIRES monitor_core mbedtls nvs_flash esp_timer freertos)

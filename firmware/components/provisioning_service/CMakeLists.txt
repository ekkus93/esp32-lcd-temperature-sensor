idf_component_register(SRCS "provisioning_service.cpp" INCLUDE_DIRS "include" REQUIRES wifi_service monitor_core nvs_flash espressif__cjson esp_driver_gpio board_support esp_timer esp_http_client)

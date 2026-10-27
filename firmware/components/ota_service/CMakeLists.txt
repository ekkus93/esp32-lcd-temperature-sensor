idf_component_register(SRCS "ota_service.cpp" INCLUDE_DIRS "include"
                       REQUIRES app_update esp_app_format esp_http_server esp_partition
                               esp_timer esp_system nvs_flash)

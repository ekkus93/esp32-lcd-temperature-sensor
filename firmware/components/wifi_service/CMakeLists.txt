idf_component_register(SRCS "wifi_service.cpp" INCLUDE_DIRS "include" REQUIRES monitor_core auth_service esp_wifi esp_event esp_netif nvs_flash esp_timer lwip espressif__mdns)

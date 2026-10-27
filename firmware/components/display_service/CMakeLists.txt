idf_component_register(SRCS "display_view.cpp" "display_runtime.cpp" INCLUDE_DIRS "include"
                       REQUIRES monitor_core lvgl__lvgl espressif__esp_lvgl_port esp_lcd
                               esp_driver_spi board_support)

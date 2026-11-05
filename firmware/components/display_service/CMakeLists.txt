idf_component_register(SRCS "display_view.cpp" "display_runtime.cpp" "fonts/font_charter_regular_12.c"
                            "fonts/font_charter_regular_14.c" "fonts/font_charter_regular_24.c"
                            "fonts/font_charter_bold_12.c" "fonts/font_charter_bold_14.c"
                            "fonts/font_charter_bold_24.c" INCLUDE_DIRS "include" "fonts"
                       REQUIRES monitor_core lvgl__lvgl espressif__esp_lvgl_port esp_lcd
                               esp_driver_spi board_support waveshare__esp_lcd_st7735 esp_timer)

#include "display_runtime.hpp"
#include "board_pins.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include <new>
#if CONFIG_MONITOR_LCD_TEST_PATTERN
#include <tuple>
#endif

namespace monitor {
namespace {
constexpr char tag[] = "display";
constexpr int width = 240;
constexpr int height = 320;
constexpr int draw_height = 40;
constexpr int spi_frequency_hz = 20000000;

#if CONFIG_MONITOR_LCD_TEST_PATTERN
lv_obj_t *diagnostic_pattern() {
    auto *root = lv_obj_create(nullptr);
    if (!root)
        return nullptr;
    lv_obj_set_size(root, width, height);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_scrollable(root, false);
    const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff};
    for (unsigned index = 0; index < 3; ++index) {
        auto *bar = lv_obj_create(root);
        if (!bar)
            return nullptr;
        lv_obj_set_pos(bar, index * (width / 3), 0);
        lv_obj_set_size(bar, width / 3, height / 2);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colors[index]), 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
    }
    auto *grid = lv_obj_create(root);
    if (!grid)
        return nullptr;
    lv_obj_set_pos(grid, 0, height / 2);
    lv_obj_set_size(grid, width, height / 2);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_color(grid, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(grid, 2, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    for (const auto &[x, y, marker] :
         {std::tuple{4, height / 2 + 4, "TL"}, std::tuple{width - 35, height / 2 + 4, "TR"},
          std::tuple{4, height - 28, "BL"}, std::tuple{width - 35, height - 28, "BR"}}) {
        auto *label = lv_label_create(root);
        if (!label)
            return nullptr;
        lv_label_set_text(label, marker);
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    }
    lv_obj_t *center = lv_label_create(root);
    if (!center)
        return nullptr;
    lv_label_set_text(center, "240 x 320");
    lv_obj_set_pos(center, 80, height / 2 + 70);
    lv_obj_set_style_text_color(center, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(center, &lv_font_montserrat_14, 0);
    return root;
}
#endif
} // namespace

bool DisplayRuntime::initialize() {
    if (ready())
        return true;

    spi_bus_config_t bus_config{};
    bus_config.sclk_io_num = static_cast<gpio_num_t>(PIN_LCD_SCLK);
    bus_config.mosi_io_num = static_cast<gpio_num_t>(PIN_LCD_MOSI);
    bus_config.miso_io_num = GPIO_NUM_NC;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    bus_config.max_transfer_sz = width * draw_height * sizeof(uint16_t);
    if (spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(tag, "LCD SPI bus initialization failed");
        deinitialize();
        return false;
    }
    spi_started_ = true;

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.dc_gpio_num = static_cast<gpio_num_t>(PIN_LCD_DC);
    io_config.cs_gpio_num = static_cast<gpio_num_t>(PIN_LCD_CS);
    io_config.pclk_hz = spi_frequency_hz;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io_) !=
        ESP_OK) {
        ESP_LOGE(tag, "LCD SPI panel IO creation failed");
        deinitialize();
        return false;
    }

    lvgl_port_cfg_t lvgl_config{};
    lvgl_config.task_priority = 4;
    lvgl_config.task_stack = 7168;
    lvgl_config.task_affinity = -1;
    lvgl_config.task_max_sleep_ms = 500;
    lvgl_config.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
    lvgl_config.timer_period_ms = 5;
    if (lvgl_port_init(&lvgl_config) != ESP_OK) {
        ESP_LOGE(tag, "LVGL port initialization failed");
        deinitialize();
        return false;
    }
    lvgl_started_ = true;

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = static_cast<gpio_num_t>(PIN_LCD_RST);
    // Generic ST7789 boards commonly use BGR; confirm color order and offsets on the panel.
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_) != ESP_OK ||
        esp_lcd_panel_reset(panel_) != ESP_OK || esp_lcd_panel_init(panel_) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK) {
        ESP_LOGE(tag, "ST7789 panel initialization failed");
        deinitialize();
        return false;
    }

    lvgl_port_display_cfg_t display_config{};
    display_config.io_handle = panel_io_;
    display_config.panel_handle = panel_;
    display_config.buffer_size = width * draw_height;
    display_config.double_buffer = true;
    display_config.hres = width;
    display_config.vres = height;
    display_config.monochrome = false;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    display_config.rotation.swap_xy = false;
    display_config.rotation.mirror_x = false;
    display_config.rotation.mirror_y = false;
    display_config.flags.buff_dma = true;
    display_config.flags.buff_spiram = true;
    display_config.flags.swap_bytes = true;
    display_ = lvgl_port_add_disp(&display_config);
    if (!display_) {
        ESP_LOGE(tag, "LVGL display registration failed");
        deinitialize();
        return false;
    }
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(tag, "LVGL lock timed out during view initialization");
        deinitialize();
        return false;
    }
    view_.reset(new (std::nothrow) DisplayView());
#if CONFIG_MONITOR_LCD_TEST_PATTERN
    if (view_)
        diagnostic_screen_ = diagnostic_pattern();
#endif
    lvgl_port_unlock();
    if (!view_
#if CONFIG_MONITOR_LCD_TEST_PATTERN
        || !diagnostic_screen_
#endif
    ) {
        ESP_LOGE(tag, "Display view allocation failed");
        deinitialize();
        return false;
    }
#if CONFIG_MONITOR_LCD_TEST_PATTERN
    if (lvgl_port_lock(1000)) {
        lv_screen_load(diagnostic_screen_);
        lvgl_port_unlock();
    }
#endif
    ESP_LOGI(tag, "ST7789/LVGL ready; internal heap=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

void DisplayRuntime::update(const Registry &registry, const TrendCache &cache,
                            const DisplayOptions &options, const DisplayStatus &status,
                            int64_t uptime_ms, bool provisioning, const std::string &ssid,
                            const std::string &wifi_key, const std::string &owner_code) {
    if (!ready() || !lvgl_port_lock(100))
        return;
#if CONFIG_MONITOR_LCD_TEST_PATTERN
    (void)registry;
    (void)cache;
    (void)options;
    (void)status;
    (void)uptime_ms;
    (void)provisioning;
    (void)ssid;
    (void)wifi_key;
    (void)owner_code;
    lvgl_port_unlock();
    return;
#else
    if (provisioning)
        view_->provisioning(ssid, wifi_key, owner_code);
    else
        view_->render(registry, cache, options, status, uptime_ms);
    lvgl_port_unlock();
#endif
}

void DisplayRuntime::deinitialize() {
    if (lvgl_started_) {
        if (!lvgl_port_lock(1000)) {
            ESP_LOGE(tag, "Could not acquire LVGL lock for cleanup");
            return;
        }
        if (diagnostic_screen_) {
            lv_obj_delete(diagnostic_screen_);
            diagnostic_screen_ = nullptr;
        }
        view_.reset();
        if (display_) {
            lvgl_port_remove_disp(display_);
            display_ = nullptr;
        }
        lvgl_port_unlock();
    }
    if (panel_) {
        esp_lcd_panel_del(panel_);
        panel_ = nullptr;
    }
    if (panel_io_) {
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }
    if (spi_started_) {
        spi_bus_free(SPI2_HOST);
        spi_started_ = false;
    }
    if (lvgl_started_) {
        lvgl_port_deinit();
        lvgl_started_ = false;
    }
}
} // namespace monitor

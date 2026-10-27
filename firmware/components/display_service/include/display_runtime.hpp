#pragma once
#include "display_view.hpp"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "monitor_types.hpp"
#include <memory>

namespace monitor {
class DisplayRuntime {
  public:
    bool initialize();
    void update(const Registry &registry, const TrendCache &cache, const DisplayOptions &options,
                const DisplayStatus &status, int64_t uptime_ms, bool provisioning = false,
                const std::string &ssid = {}, const std::string &wifi_key = {},
                const std::string &owner_code = {});
    bool ready() const {
        return display_ != nullptr && view_ != nullptr;
    }

  private:
    void deinitialize();
    bool lvgl_started_ = false;
    bool spi_started_ = false;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    lv_display_t *display_ = nullptr;
    lv_obj_t *diagnostic_screen_ = nullptr;
    std::unique_ptr<DisplayView> view_;
};
} // namespace monitor

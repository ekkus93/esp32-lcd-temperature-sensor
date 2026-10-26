#pragma once
#include "display_model.hpp"
#include "lvgl.h"
namespace monitor {
// Call only from one LVGL context, under esp_lvgl_port's lock on the device.
// Construct once after a display is registered; no GPIO or panel initialization here.
class DisplayView {
  public:
    DisplayView();
    ~DisplayView();
    void render(const Registry &registry, const TrendCache &cache, const DisplayOptions &options,
                const DisplayStatus &status, int64_t uptime_ms);
    void provisioning(const std::string &ssid, const std::string &wifi_key,
                      const std::string &owner_code);
    bool showing_trends() const {
        return showing_trends_;
    }

  private:
    struct Screen {
        lv_obj_t *root, *time, *footer;
        std::array<lv_obj_t *, max_sensors> labels{}, values{}, states{}, charts{};
        std::array<lv_obj_t *, max_sensors> no_data{};
        std::array<lv_chart_series_t *, max_sensors> series{};
    };
    Screen overview_{}, trends_{};
    lv_obj_t *setup_ = nullptr, *setup_text_ = nullptr;
    DisplayCycle cycle_;
    DisplayOptions options_;
    bool configured_ = false, showing_trends_ = false;
    int64_t cache_window_ = -1;
    std::array<uint64_t, max_sensors> chart_roms_{};
    std::array<bool, max_sensors> chart_data_{};
    size_t cache_size_ = 0;
};
} // namespace monitor

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
    // Shows the setup screen. On the compact screen `owner_page` picks the owner-code page instead
    // of the Wi-Fi page; it is ignored when there is no owner code or on the large screen.
    // `local_name` (for example "temperature-monitor.local") is shown as an alternative to the
    // setup address when the device is also on the home network; empty hides it.
    void provisioning(const std::string &ssid, const std::string &wifi_key,
                      const std::string &owner_code, bool owner_page = false,
                      const std::string &local_name = {});
    bool showing_trends() const {
        return showing_trends_;
    }
    // Handles to individual widgets, so tests can find them by role instead of by the order they
    // were created in.
    lv_obj_t *wifi_icon() const {
        return showing_trends_ ? trends_.wifi : overview_.wifi;
    }
    lv_obj_t *footer_label() const {
        return showing_trends_ ? trends_.footer : overview_.footer;
    }
    lv_obj_t *trends_label(size_t sensor) const {
        return trends_.labels[sensor];
    }
    lv_obj_t *trends_chart(size_t sensor) const {
        return trends_.charts[sensor];
    }
    lv_obj_t *trends_legend(size_t sensor) const {
        return trends_.legend[sensor];
    }
    lv_obj_t *trends_no_data() const {
        return trends_.no_data[0];
    }
    lv_obj_t *trends_value(size_t sensor) const {
        return trends_.values[sensor];
    }
    lv_obj_t *trends_axis_max(size_t sensor) const {
        return trends_.axis_max[sensor];
    }
    lv_obj_t *trends_status(size_t sensor) const {
        return trends_.states[sensor];
    }

  private:
    struct Screen {
        lv_obj_t *root, *time, *footer, *title = nullptr;
        // Compact screens: Wi-Fi state as an icon beside the time (colour tells the state).
        lv_obj_t *wifi = nullptr;
        // Compact Trends only: highest and lowest value on each chart, and the time axis ends.
        lv_obj_t *axis_from = nullptr, *axis_now = nullptr;
        std::array<lv_obj_t *, 5> axis_max{}, axis_min{}, legend{};
        std::array<lv_obj_t *, max_sensors> labels{}, values{}, alts{}, states{}, charts{};
        std::array<lv_obj_t *, max_sensors> no_data{};
        std::array<lv_chart_series_t *, max_sensors> series{};
    };
    Screen overview_{}, trends_{};
    // Setup screen: title, "1 join" heading/SSID, password heading/value, "2 open" heading/URL,
    // owner-code heading/value, a hint and a page footer, shown or hidden per layout and page.
    static constexpr size_t setup_slot_count = 11;
    lv_obj_t *setup_ = nullptr;
    std::array<lv_obj_t *, setup_slot_count> setup_slots_{};
    // Decided once from the display resolution when the view is built.
    bool compact_ = false;
    DisplayCycle cycle_;
    DisplayOptions options_;
    bool configured_ = false, showing_trends_ = false;
    // Compact Trends: each probe's own range and the combined range last drawn.
    std::array<int32_t, max_sensors> chart_low_{}, chart_high_{};
    int32_t trend_low_ = INT32_MAX, trend_high_ = INT32_MIN;
    std::array<bool, max_sensors> series_hidden_{};
    int64_t cache_window_ = -1;
    std::array<uint64_t, max_sensors> chart_roms_{};
    std::array<bool, max_sensors> chart_data_{};
    size_t cache_size_ = 0;
};
} // namespace monitor

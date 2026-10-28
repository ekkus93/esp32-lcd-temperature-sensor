#include "display_view.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace monitor {
namespace {
constexpr uint32_t background = 0x101b20, foreground = 0xf1f5f6;
lv_obj_t *screen() {
    auto *root = lv_obj_create(nullptr);
    lv_obj_set_size(root, 240, 320);
    lv_obj_set_style_bg_color(root, lv_color_hex(background), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(foreground), 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_clickable(root, false);
    return root;
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font) {
    auto *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(obj, font, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}
void text(lv_obj_t *obj, const std::string &value) {
    if (std::strcmp(lv_label_get_text(obj), value.c_str()))
        lv_label_set_text(obj, value.c_str());
}
uint32_t color(State state) {
    switch (state) {
    case State::high:
        return 0xffaaa0;
    case State::low:
        return 0xffd583;
    case State::disabled:
    case State::unassigned:
        return 0x9daeb6;
    case State::disconnected:
    case State::invalid:
    case State::stale:
    case State::bus_fault:
        return 0xffd583;
    default:
        return 0x9be3c4;
    }
}
std::string wrapped(const std::string &value) {
    std::string output;
    for (size_t i = 0; i < value.size(); i += 16) {
        if (i)
            output += '\n';
        output += value.substr(i, 16);
    }
    return output;
}
} // namespace
DisplayView::DisplayView() {
    for (auto *s : {&overview_, &trends_}) {
        s->root = screen();
        text(label(s->root, 10, 10, 160, &lv_font_montserrat_14),
             s == &overview_ ? "Temp Monitor" : "Temperature trends");
        s->time = label(s->root, 183, 10, 50, &lv_font_montserrat_14);
        s->footer = label(s->root, 10, 281, 225, &lv_font_montserrat_14);
        lv_obj_set_height(s->footer, 38);
        lv_label_set_long_mode(s->footer, LV_LABEL_LONG_WRAP);
        for (unsigned i = 0; i < max_sensors; ++i) {
            const int top = 40 + 47 * i;
            s->labels[i] = label(s->root, 10, top, 98, &lv_font_montserrat_14);
            s->values[i] = label(s->root, 114, top, 116,
                                 s == &overview_ ? &lv_font_montserrat_24 : &lv_font_montserrat_14);
            lv_obj_set_style_text_align(s->values[i], LV_TEXT_ALIGN_RIGHT, 0);
            s->states[i] = label(s->root, s == &overview_ ? 10 : 160, top + 24,
                                 s == &overview_ ? 210 : 72, &lv_font_montserrat_12);
            if (s == &trends_) {
                auto *chart = lv_chart_create(s->root);
                s->charts[i] = chart;
                lv_obj_set_pos(chart, 12, top + 21);
                lv_obj_set_size(chart, 141, 23);
                lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(chart, 0, 0);
                lv_obj_set_style_pad_all(chart, 0, 0);
                lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
                lv_chart_set_div_line_count(chart, 0, 0);
                lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
                lv_obj_set_scrollable(chart, false);
                lv_obj_set_clickable(chart, false);
                s->series[i] =
                    lv_chart_add_series(chart, lv_color_hex(0x9be3c4), LV_CHART_AXIS_PRIMARY_Y);
                s->no_data[i] = label(s->root, 12, top + 24, 141, &lv_font_montserrat_12);
                text(s->no_data[i], "NO DATA");
            }
        }
    }
    setup_ = screen();
    text(label(setup_, 10, 10, 220, &lv_font_montserrat_14), "Wi-Fi setup (read-only)");
    setup_text_ = label(setup_, 10, 38, 220, &lv_font_montserrat_14);
    lv_obj_set_height(setup_text_, 280);
    lv_label_set_long_mode(setup_text_, LV_LABEL_LONG_WRAP);
    lv_screen_load(overview_.root);
}
DisplayView::~DisplayView() {
    // The caller must load another screen before destroying the active view.
    lv_obj_delete(overview_.root);
    lv_obj_delete(trends_.root);
    lv_obj_delete(setup_);
}
void DisplayView::render(const Registry &registry, const TrendCache &cache,
                         const DisplayOptions &options, const DisplayStatus &status, int64_t now) {
    if (!valid_display_options(options))
        return;
    const bool changed = !configured_ || options.extended != options_.extended ||
                         options.overview_seconds != options_.overview_seconds ||
                         options.trends_seconds != options_.trends_seconds;
    const bool plots_changed = changed || cache_window_ != now / bucket_ms ||
                               cache_size_ != cache.size() ||
                               options.fahrenheit != options_.fahrenheit ||
                               options.lookback_minutes != options_.lookback_minutes;
    if (changed)
        cycle_.configure(options.extended, options.overview_seconds, options.trends_seconds, now);
    options_ = options;
    configured_ = true;
    const auto rows = display_rows(registry, options.fahrenheit);
    for (unsigned i = 0; i < max_sensors; ++i) {
        const auto &row = rows[i];
        for (auto *s : {&overview_, &trends_}) {
            text(s->labels[i], row.label);
            text(s->values[i], row.value);
            lv_obj_set_style_text_color(s->values[i], lv_color_hex(color(row.state)), 0);
            text(s->states[i], row.status);
            lv_obj_set_style_text_color(s->states[i], lv_color_hex(color(row.state)), 0);
        }
        if (plots_changed || chart_roms_[i] != row.rom) {
            const auto values = cache.values(row.rom, now, options.lookback_minutes);
            std::array<int32_t, 288> points{};
            points.fill(LV_CHART_POINT_NONE);
            int32_t low = INT32_MAX, high = INT32_MIN;
            for (size_t p = 0; p < values.size(); ++p)
                if (values[p]) {
                    const double value = options.fahrenheit ? *values[p] * 1.8 + 32 : *values[p];
                    points[p] = int32_t(std::round(value * 100));
                    low = std::min(low, points[p]);
                    high = std::max(high, points[p]);
                }
            chart_data_[i] = low != INT32_MAX;
            lv_chart_set_point_count(trends_.charts[i], values.size());
            lv_chart_set_series_values(trends_.charts[i], trends_.series[i], points.data(),
                                       values.size());
            lv_chart_set_axis_range(trends_.charts[i], LV_CHART_AXIS_PRIMARY_Y,
                                    low == INT32_MAX ? 0 : low - 50,
                                    high == INT32_MIN ? 100 : high + 50);
            chart_roms_[i] = row.rom;
        }
        const bool show_chart = chart_data_[i] && row.state != State::disabled && row.rom;
        lv_obj_set_hidden(trends_.charts[i], !show_chart);
        lv_obj_set_hidden(trends_.no_data[i], show_chart);
        text(trends_.no_data[i], row.state == State::disabled ? "OFF" : "NO DATA");
        if (row.state == State::disabled)
            text(trends_.states[i], "OFF");
        else if (row.status.empty() && !chart_data_[i])
            text(trends_.states[i], "NO DATA");
    }
    cache_window_ = now / bucket_ms;
    cache_size_ = cache.size();
    const std::string clock = status.local_time.empty() ? "--:--" : status.local_time;
    std::string footer = "Wi-Fi " + status.wifi + (status.logging ? " | Log OK" : " | Log ERR");
    if (registry.bus_fault())
        footer += "\nBUS FAULT";
    else if (status.button_progress)
        footer += "\nHold BOOT " + std::to_string(std::min(100u, status.button_progress)) + "%";
    else if (status.ota_state == "new" || status.ota_state == "pending_verify")
        footer += "\nOTA VERIFYING";
    else if (status.ota_state == "invalid" || status.ota_state == "aborted")
        footer += "\nOTA ROLLBACK";
    else
        footer += "\nPast " + std::to_string(options.lookback_minutes) + " min";
    for (auto *s : {&overview_, &trends_}) {
        text(s->time, clock);
        text(s->footer, footer);
    }
    showing_trends_ = cycle_.trends(now);
    auto *desired = showing_trends_ ? trends_.root : overview_.root;
    if (lv_screen_active() != desired)
        lv_screen_load(desired);
}
void DisplayView::provisioning(const std::string &ssid, const std::string &wifi_key,
                               const std::string &owner_code) {
    text(setup_text_, "Connect to:\n" + ssid + "\nWi-Fi key:\n" + wrapped(wifi_key) +
                          "\nOpen 192.168.4.1\nOwner code:\n" + wrapped(owner_code));
    if (lv_screen_active() != setup_)
        lv_screen_load(setup_);
}
} // namespace monitor

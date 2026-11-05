#include "display_view.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace monitor {
namespace {
constexpr uint32_t background = 0x101b20, foreground = 0xf1f5f6;
constexpr uint32_t setup_muted = 0x9daeb6, setup_accent = 0x8fe3c8;
lv_obj_t *screen() {
    auto *root = lv_obj_create(nullptr);
    lv_obj_set_size(root, lv_display_get_horizontal_resolution(nullptr),
                    lv_display_get_vertical_resolution(nullptr));
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
void text_color(lv_obj_t *obj, uint32_t value) {
    const auto desired = lv_color_hex(value);
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), desired))
        lv_obj_set_style_text_color(obj, desired, 0);
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
} // namespace
DisplayView::DisplayView() {
    compact_ = lv_display_get_horizontal_resolution(nullptr) <= 128;
    const bool compact = compact_;
    for (auto *s : {&overview_, &trends_}) {
        s->root = screen();
        text(label(s->root, compact ? 2 : 10, compact ? 0 : 10, compact ? 76 : 160,
                   compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14),
             s == &overview_ ? (compact ? "Temps" : "Temp Monitor")
                             : (compact ? "Trends" : "Temperature trends"));
        s->time = label(s->root, compact ? 82 : 183, compact ? 0 : 10, compact ? 44 : 50,
                        compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14);
        s->footer = label(s->root, compact ? 2 : 10, compact ? 141 : 281, compact ? 124 : 225,
                          compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14);
        lv_obj_set_height(s->footer, compact ? 19 : 38);
        lv_label_set_long_mode(s->footer, LV_LABEL_LONG_WRAP);
        for (unsigned i = 0; i < max_sensors; ++i) {
            const bool large_trend = compact && s == &trends_;
            const int top = large_trend ? 20 : (compact ? 17 + 24 * i : 40 + 47 * i);
            s->labels[i] =
                label(s->root, compact ? 2 : 10, top, large_trend ? 70 : (compact ? 58 : 98),
                      compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14);
            s->values[i] = label(
                s->root, large_trend ? 74 : (compact ? 60 : 114), top,
                large_trend ? 52 : (compact ? 66 : 116),
                compact ? &lv_font_montserrat_12
                        : (s == &overview_ ? &lv_font_montserrat_24 : &lv_font_montserrat_14));
            lv_obj_set_style_text_align(s->values[i], LV_TEXT_ALIGN_RIGHT, 0);
            if (s == &overview_) {
                // The same reading in the other unit, under the main value; hidden unless the row
                // has a numeric reading. A HIGH or LOW row shows both on one line: that works
                // because the status text is short and left-aligned.
                s->alts[i] = label(s->root, compact ? 60 : 114, top + (compact ? 12 : 26),
                                   compact ? 66 : 116,
                                   compact ? &lv_font_montserrat_12 : &lv_font_montserrat_14);
                lv_obj_set_style_text_align(s->alts[i], LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_set_style_text_color(s->alts[i], lv_color_hex(setup_muted), 0);
                lv_obj_set_hidden(s->alts[i], true);
            }
            s->states[i] =
                label(s->root, large_trend ? 2 : (compact ? 2 : (s == &overview_ ? 10 : 160)),
                      large_trend ? 122 : top + (compact ? 12 : 24),
                      large_trend ? 124 : (compact ? 124 : (s == &overview_ ? 210 : 72)),
                      &lv_font_montserrat_12);
            if (s == &trends_) {
                auto *chart = lv_chart_create(s->root);
                s->charts[i] = chart;
                lv_obj_set_pos(chart, large_trend ? 4 : 12, large_trend ? 42 : top + 21);
                lv_obj_set_size(chart, large_trend ? 120 : 141, large_trend ? 75 : 23);
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
                s->no_data[i] = label(s->root, large_trend ? 4 : 12, large_trend ? 70 : top + 24,
                                      large_trend ? 120 : 141, &lv_font_montserrat_12);
                text(s->no_data[i], "NO DATA");
            }
        }
    }
    setup_ = screen();
    // Slots: 0 title, 1 step-1 heading, 2 SSID, 3 password heading, 4 password, 5 step-2
    // heading, 6 address, 7 owner-code heading, 8 owner code, 9 page footer, 10 hint.
    struct SlotLayout {
        int x, y, width, height;
        const lv_font_t *font;
        uint32_t color;
    };
    const auto &small = compact ? lv_font_montserrat_12 : lv_font_montserrat_14;
    const auto &medium = lv_font_montserrat_14;
    const auto &large = compact ? lv_font_montserrat_14 : lv_font_montserrat_24;
    const int x = compact ? 2 : 10, w = compact ? 124 : 220;
    using SlotLayouts = std::array<SlotLayout, setup_slot_count>;
    const SlotLayouts compact_layout{{
        {x, 0, w, 16, &medium, foreground},    // title
        {x, 20, w, 14, &small, setup_muted},   // 1 Join this Wi-Fi
        {x, 35, w, 30, &small, foreground},    // SSID, two lines
        {x, 68, w, 14, &small, setup_muted},   // Password
        {x, 83, w, 18, &large, setup_accent},  // password value
        {x, 106, w, 14, &small, setup_muted},  // 2 Open in a browser
        {x, 121, w, 18, &large, setup_accent}, // address
        {x, 20, w, 14, &small, setup_muted},   // owner heading (page 2)
        {x, 35, w, 36, &large, setup_accent},  // owner code, two lines (page 2)
        {x, 143, w, 14, &small, setup_muted},  // page footer
        {x, 84, w, 32, &small, setup_muted},   // hint (page 2)
    }};
    const SlotLayouts large_layout{{
        {x, 10, w, 18, &medium, foreground},   // title
        {x, 44, w, 16, &small, setup_muted},   // 1 Join this Wi-Fi
        {x, 64, w, 18, &medium, foreground},   // SSID
        {x, 98, w, 16, &small, setup_muted},   // Password
        {x, 118, w, 30, &large, setup_accent}, // password value
        {x, 162, w, 16, &small, setup_muted},  // 2 Open in a browser
        {x, 182, w, 30, &large, setup_accent}, // address
        {x, 226, w, 16, &small, setup_muted},  // 3 Owner code
        {x, 246, w, 60, &large, setup_accent}, // owner code, two lines
        {x, 300, w, 16, &small, setup_muted},  // unused on this layout
        {x, 300, w, 16, &small, setup_muted},  // unused on this layout
    }};
    const SlotLayouts &layouts = compact ? compact_layout : large_layout;
    for (size_t i = 0; i < setup_slot_count; ++i) {
        setup_slots_[i] =
            label(setup_, layouts[i].x, layouts[i].y, layouts[i].width, layouts[i].font);
        lv_obj_set_height(setup_slots_[i], layouts[i].height);
        lv_label_set_long_mode(setup_slots_[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(setup_slots_[i], lv_color_hex(layouts[i].color), 0);
    }
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
    const auto rows = display_rows(registry, options.fahrenheit);
    const bool compact = compact_;
    const bool next_trends = cycle_.trends(now);
    if (compact && configured_ && showing_trends_ && next_trends && now >= last_render_ms_ &&
        now - last_render_ms_ <= int64_t(options.trends_seconds) * 1000)
        compact_trend_visible_ms_ += now - last_render_ms_;
    last_render_ms_ = now;
    configured_ = true;
    std::array<size_t, max_sensors> compact_rows{};
    size_t compact_count = 0;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].rom)
            compact_rows[compact_count++] = i;
    const size_t compact_position =
        compact_count ? (compact_trend_visible_ms_ / 5000) % compact_count : 0;
    const size_t selected_row = compact_count ? compact_rows[compact_position] : 0;
    for (unsigned i = 0; i < max_sensors; ++i) {
        const auto &row = rows[i];
        for (auto *s : {&overview_, &trends_}) {
            text(s->labels[i], row.label);
            text(s->values[i], row.value);
            text_color(s->values[i], color(row.state));
            // The Trends status text depends on chart data computed below; setting it here and
            // overwriting it later flipped the label twice on every refresh.
            if (s == &overview_) {
                text(s->states[i], row.status);
                text(s->alts[i], row.alt_value);
                lv_obj_set_hidden(s->alts[i], row.alt_value.empty());
            }
            text_color(s->states[i], color(row.state));
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
        const bool selected = !compact || i == selected_row;
        if (compact) {
            lv_obj_set_hidden(trends_.labels[i], !selected);
            lv_obj_set_hidden(trends_.values[i], !selected);
            lv_obj_set_hidden(trends_.states[i], !selected);
        }
        const bool show_chart = chart_data_[i] && row.state != State::disabled && row.rom;
        lv_obj_set_hidden(trends_.charts[i], !selected || !show_chart);
        lv_obj_set_hidden(trends_.no_data[i], !selected || show_chart);
        text(trends_.no_data[i], row.state == State::disabled ? "OFF" : "NO DATA");
        const char *trends_status = row.status.c_str();
        if (row.state == State::disabled)
            trends_status = "OFF";
        else if (row.status.empty() && !chart_data_[i])
            trends_status = "NO DATA";
        text(trends_.states[i], trends_status);
    }
    cache_window_ = now / bucket_ms;
    cache_size_ = cache.size();
    const std::string clock = status.local_time.empty() ? "--:--" : status.local_time;
    // Urgent conditions replace the usual footer detail. Priority: a BOOT hold in progress (the
    // owner is acting on it right now), then a bus fault, then an OTA state.
    std::string urgent;
    if (status.button_progress)
        urgent = "Hold BOOT " + std::to_string(std::min(100u, status.button_progress)) + "%";
    else if (registry.bus_fault())
        urgent = "BUS FAULT";
    else if (status.ota_state == "new" || status.ota_state == "pending_verify")
        urgent = "OTA VERIFYING";
    else if (status.ota_state == "invalid" || status.ota_state == "aborted")
        urgent = "OTA ROLLBACK";
    const std::string status_line =
        "Wi-Fi " + status.wifi + (status.logging ? " | Log OK" : " | Log ERR");
    // The Overview has no lookback window to describe, so on the large screen its second line
    // appears only for an urgent condition; the Trends screen shows "Past N min" otherwise.
    std::string overview_footer, trends_footer;
    if (compact_) {
        // One line only: an urgent condition, else a logging failure (it must be visible on the
        // small screen), else the Wi-Fi state. Trends shows its lookback and chart position
        // instead of the Wi-Fi state when nothing needs attention.
        overview_footer = !urgent.empty()   ? urgent
                          : !status.logging ? "Log ERR"
                                            : "Wi-Fi " + status.wifi;
        trends_footer = overview_footer.rfind("Wi-Fi ", 0) == 0
                            ? std::to_string(options.lookback_minutes) + "m  " +
                                  std::to_string(compact_position + 1) + "/" +
                                  std::to_string(std::max<size_t>(compact_count, 1))
                            : overview_footer;
    } else {
        overview_footer = urgent.empty() ? status_line : status_line + "\n" + urgent;
        trends_footer =
            status_line + "\n" +
            (urgent.empty() ? "Past " + std::to_string(options.lookback_minutes) + " min" : urgent);
    }
    text(overview_.time, clock);
    text(trends_.time, clock);
    text(overview_.footer, overview_footer);
    text(trends_.footer, trends_footer);
    showing_trends_ = next_trends;
    auto *desired = showing_trends_ ? trends_.root : overview_.root;
    if (lv_screen_active() != desired)
        lv_screen_load(desired);
}
void DisplayView::provisioning(const std::string &ssid, const std::string &wifi_key,
                               const std::string &owner_code, bool owner_page_requested) {
    const bool compact = compact_;
    const bool has_owner_code = !owner_code.empty();
    // The compact screen alternates between the Wi-Fi page and the owner-code page, but only when
    // there is an owner code to show (an enrolled device has none).
    const bool owner_page = compact && has_owner_code && owner_page_requested;
    // Codes are wrapped in two equal lines so each line fits the screen without breaking a word.
    auto two_lines = [](const std::string &code) {
        const size_t half = (code.size() + 1) / 2;
        return code.size() <= 8 ? code : code.substr(0, half) + "\n" + code.substr(half);
    };
    auto set = [&](size_t slot, const std::string &value) {
        text(setup_slots_[slot], value);
        lv_obj_set_hidden(setup_slots_[slot], value.empty());
    };
    const std::string title = owner_page ? "Owner code" : "Wi-Fi setup";
    if (compact && owner_page) {
        for (size_t slot : {size_t(1), size_t(2), size_t(3), size_t(4), size_t(5), size_t(6)})
            set(slot, "");
        set(0, title);
        set(7, "Owner setup code:");
        set(8, two_lines(owner_code));
        set(10, "Type it once at\n192.168.4.1");
        set(9, "2/2");
    } else {
        set(0, title);
        set(1, "1  Join this Wi-Fi");
        // "TempMonitor-XXXXXX" is wider than the compact screen, so break it after the hyphen
        // instead of letting it wrap or clip unpredictably.
        const auto dash = ssid.find('-');
        set(2, compact && dash != std::string::npos && dash + 1 < ssid.size()
                   ? ssid.substr(0, dash + 1) + "\n" + ssid.substr(dash + 1)
                   : ssid);
        set(3, "Password");
        set(4, wifi_key);
        set(5, "2  Open in a browser");
        set(6, "192.168.4.1");
        set(7, compact ? "" : "3  One-time owner code");
        set(8, compact ? "" : two_lines(owner_code));
        set(10, "");
        set(9, compact && has_owner_code ? "1/2" : "");
        if (!compact && !has_owner_code)
            set(7, "");
    }
    if (lv_screen_active() != setup_)
        lv_screen_load(setup_);
}
} // namespace monitor

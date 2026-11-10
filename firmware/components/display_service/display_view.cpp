#include "display_view.hpp"
#include "charter_fonts.h"
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
// One colour per probe position (the Trends legend names them): sky blue, amber, coral, lilac,
// mint. They stay distinct on the dark background and are not the alert colours.
constexpr int legend_top = 103, legend_row = 12; // compact Trends legend: first row, row pitch
constexpr uint32_t line_colors[max_sensors] = {0x7fc8ff, 0xffc857, 0xff8a80, 0xc3a6ff, 0x7fe0b0};
// Scale bounds for the compact graph, in hundredths of the displayed unit. The bounds are round
// numbers (a whole number of steps, an even count so the middle tick is round too), they always
// contain the data, and they span at least 2 degrees so a steady reading does not look like a
// steep slope.
struct Axis {
    int32_t low, high;
};
int32_t floor_div(int32_t a, int32_t b) {
    return a / b - (a % b != 0 && ((a < 0) != (b < 0)) ? 1 : 0);
}
Axis nice_axis(int32_t data_low, int32_t data_high) {
    const int32_t minimum_span = 200;
    const int32_t span = std::max(data_high - data_low, minimum_span);
    const int32_t step = span <= 300 ? 50 : span <= 1000 ? 100 : span <= 3000 ? 500 : 1000;
    const int32_t middle = (data_low + data_high) / 2;
    int32_t low = floor_div(middle - span / 2, step) * step;
    int32_t high = -floor_div(-(middle + span / 2), step) * step;
    while (low > data_low)
        low -= step;
    while (high < data_high)
        high += step;
    if (((high - low) / step) % 2)
        high += step;
    return {low, high};
}
} // namespace
DisplayView::DisplayView() {
    compact_ = lv_display_get_horizontal_resolution(nullptr) <= 128;
    const bool compact = compact_;
    for (auto *s : {&overview_, &trends_}) {
        s->root = screen();
        s->title = label(s->root, compact ? 2 : 10, compact ? 0 : 10, compact ? 76 : 160,
                         compact ? &font_charter_bold_12 : &font_charter_bold_14);
        text(s->title, s == &overview_ ? (compact ? "Temps" : "Temp Monitor")
                                       : (compact ? "Trends" : "Temperature trends"));
        // On the compact screen the time makes room for a Wi-Fi icon at the right edge.
        s->time = label(s->root, compact ? 72 : 183, compact ? 0 : 10, compact ? 38 : 50,
                        compact ? &font_charter_regular_12 : &font_charter_regular_14);
        if (compact)
            s->wifi = label(s->root, 112, 0, 14, &lv_font_montserrat_12);
        s->footer = label(s->root, compact ? 2 : 10, compact ? 141 : 281, compact ? 124 : 225,
                          compact ? &font_charter_regular_12 : &font_charter_regular_14);
        lv_obj_set_height(s->footer, compact ? 19 : 38);
        lv_label_set_long_mode(s->footer, LV_LABEL_LONG_WRAP);
        for (unsigned i = 0; i < max_sensors; ++i) {
            const bool large_trend = compact && s == &trends_;
            const int top = large_trend ? 20 : (compact ? 17 + 24 * i : 40 + 47 * i);
            s->labels[i] =
                label(s->root, compact ? 2 : 10, top, large_trend ? 70 : (compact ? 58 : 98),
                      compact ? &font_charter_regular_12 : &font_charter_regular_14);
            s->values[i] =
                label(s->root, large_trend ? 74 : (compact ? 60 : 114), top,
                      large_trend ? 52 : (compact ? 66 : 116),
                      compact ? &font_charter_bold_12
                              : (s == &overview_ ? &font_charter_bold_24 : &font_charter_bold_14));
            lv_obj_set_style_text_align(s->values[i], LV_TEXT_ALIGN_RIGHT, 0);
            if (s == &overview_) {
                // The same reading in the other unit, under the main value; hidden unless the row
                // has a numeric reading. A HIGH or LOW row shows both on one line: that works
                // because the status text is short and left-aligned.
                s->alts[i] = label(s->root, compact ? 60 : 114, top + (compact ? 12 : 26),
                                   compact ? 66 : 116,
                                   compact ? &font_charter_regular_12 : &font_charter_regular_14);
                lv_obj_set_style_text_align(s->alts[i], LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_set_style_text_color(s->alts[i], lv_color_hex(setup_muted), 0);
                lv_obj_set_hidden(s->alts[i], true);
            }
            s->states[i] =
                label(s->root, large_trend ? 2 : (compact ? 2 : (s == &overview_ ? 10 : 160)),
                      large_trend ? 122 : top + (compact ? 12 : 24),
                      large_trend ? 124 : (compact ? 124 : (s == &overview_ ? 210 : 72)),
                      &font_charter_regular_12);
            if (s == &trends_) {
                // Compact: one chart for every probe (created with the first), each probe a line
                // of its own colour. Large screen: a small chart in each probe's row.
                lv_obj_t *chart = nullptr;
                if (large_trend && i > 0) {
                    chart = s->charts[0];
                } else {
                    chart = lv_chart_create(s->root);
                    lv_obj_set_pos(chart, large_trend ? 38 : 12, large_trend ? 22 : top + 21);
                    lv_obj_set_size(chart, large_trend ? 86 : 141, large_trend ? 64 : 23);
                    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
                    // Compact: the left and bottom edges are the axes, with faint grid lines.
                    lv_obj_set_style_border_width(chart, large_trend ? 1 : 0, 0);
                    if (large_trend) {
                        lv_obj_set_style_border_side(
                            chart, lv_border_side_t(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM),
                            0);
                        lv_obj_set_style_border_color(chart, lv_color_hex(setup_muted), 0);
                        lv_obj_set_style_line_color(chart, lv_color_hex(0x2c3a40), LV_PART_MAIN);
                        lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
                    }
                    lv_obj_set_style_pad_all(chart, 0, 0);
                    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
                    lv_obj_set_style_line_width(chart, large_trend ? 2 : 1, LV_PART_ITEMS);
                    lv_chart_set_div_line_count(chart, large_trend ? 3 : 0, large_trend ? 3 : 0);
                    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
                    lv_obj_set_scrollable(chart, false);
                    lv_obj_set_clickable(chart, false);
                }
                s->charts[i] = chart;
                s->series[i] = lv_chart_add_series(
                    chart, lv_color_hex(large_trend ? line_colors[i] : 0x9be3c4),
                    LV_CHART_AXIS_PRIMARY_Y);
                if (large_trend) {
                    // The legend: probe names in their line colours, in two balanced columns
                    // under the chart (positioned in render() from the number of probes).
                    s->legend[i] = label(s->root, 2, legend_top, 62, &font_charter_regular_12);
                    lv_label_set_long_mode(s->legend[i], LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_color(s->legend[i], lv_color_hex(line_colors[i]), 0);
                    if (i == 0) {
                        for (auto *axis : {&s->axis_max[0], &s->axis_mid, &s->axis_min[0]}) {
                            *axis = label(s->root, 0,
                                          axis == &s->axis_max[0] ? 16
                                          : axis == &s->axis_mid  ? 48
                                                                  : 80,
                                          36, &font_charter_regular_12);
                            lv_obj_set_style_text_align(*axis, LV_TEXT_ALIGN_RIGHT, 0);
                            lv_obj_set_style_text_color(*axis, lv_color_hex(setup_muted), 0);
                        }
                    }
                }
                if (!large_trend || i == 0) {
                    s->no_data[i] =
                        label(s->root, large_trend ? 4 : 12, large_trend ? 48 : top + 24,
                              large_trend ? 120 : 141, &font_charter_regular_12);
                    text(s->no_data[i], "NO DATA");
                }
            }
        }
        if (compact && s == &trends_) {
            s->axis_from = label(s->root, 38, 88, 43, &font_charter_regular_12);
            s->axis_mid_x = label(s->root, 59, 88, 43, &font_charter_regular_12);
            s->axis_now = label(s->root, 81, 88, 43, &font_charter_regular_12);
            lv_obj_set_style_text_align(s->axis_mid_x, LV_TEXT_ALIGN_CENTER, 0);
            // At most three legend rows end at y=139, so the Trends footer sits below them.
            lv_obj_set_y(s->footer, 147);
            lv_obj_set_height(s->footer, 13);
            lv_obj_set_style_text_align(s->axis_now, LV_TEXT_ALIGN_RIGHT, 0);
            for (auto *axis : {s->axis_from, s->axis_mid_x, s->axis_now})
                lv_obj_set_style_text_color(axis, lv_color_hex(setup_muted), 0);
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
    configured_ = true;
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
        const bool replot = plots_changed || chart_roms_[i] != row.rom;
        if (replot) {
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
            chart_points_[i] =
                size_t(std::count_if(points.begin(), points.begin() + values.size(),
                                     [](int32_t p) { return p != LV_CHART_POINT_NONE; }));
            chart_low_[i] = low;
            chart_high_[i] = high;
            chart_roms_[i] = row.rom;
            // On the compact screen every probe is a series of the one shared chart, which has
            // one point count and one scale (set below); each large-screen row has its own chart.
            if (compact) {
                lv_chart_set_point_count(trends_.charts[0], (options.lookback_minutes + 4) / 5);
            } else {
                lv_chart_set_point_count(trends_.charts[i], values.size());
                lv_chart_set_axis_range(trends_.charts[i], LV_CHART_AXIS_PRIMARY_Y,
                                        low == INT32_MAX ? 0 : low - 50,
                                        high == INT32_MIN ? 100 : high + 50);
            }
            monitor::bridge_short_gaps(points.data(), values.size(), LV_CHART_POINT_NONE, 2);
            lv_chart_set_series_values(trends_.charts[i], trends_.series[i], points.data(),
                                       values.size());
        }
        if (compact)
            continue; // the compact Trends screen is drawn once after the loop
        const bool show_chart = chart_data_[i] && row.state != State::disabled && row.rom;
        lv_obj_set_hidden(trends_.charts[i], !show_chart);
        lv_obj_set_hidden(trends_.no_data[i], show_chart);
        text(trends_.no_data[i], row.state == State::disabled ? "OFF" : "NO DATA");
        const char *trends_status = row.status.c_str();
        if (row.state == State::disabled)
            trends_status = "OFF";
        else if (row.status.empty() && !chart_data_[i])
            trends_status = "NO DATA";
        text(trends_.states[i], trends_status);
    }
    if (compact) {
        // One chart, one line per probe with data, one scale covering all of them.
        int32_t low = INT32_MAX, high = INT32_MIN;
        for (unsigned i = 0; i < max_sensors; ++i) {
            const bool plotted = chart_data_[i] && rows[i].rom && rows[i].state != State::disabled;
            if (series_hidden_[i] == plotted) { // only on a change: the call always repaints
                series_hidden_[i] = !plotted;
                lv_chart_hide_series(trends_.charts[0], trends_.series[i], !plotted);
            }
            if (plotted) {
                low = std::min(low, chart_low_[i]);
                high = std::max(high, chart_high_[i]);
            }
            // Legend: the probe's name, always in the colour of its line (also before it has data,
            // so the colour can be matched as soon as the line appears).
            lv_obj_set_hidden(trends_.legend[i], rows[i].rom == 0);
            text(trends_.legend[i], rows[i].label);
            text_color(trends_.legend[i], line_colors[i]);
            // The per-probe header and status used on the large screen are not shown here.
            lv_obj_set_hidden(trends_.labels[i], true);
            lv_obj_set_hidden(trends_.values[i], true);
            lv_obj_set_hidden(trends_.states[i], true);
        }
        // Balanced columns: the first half (rounded up) on the left, the rest on the right.
        size_t listed = 0;
        for (const auto &row : rows)
            listed += row.rom != 0;
        if (listed != legend_count_) {
            legend_count_ = listed;
            const size_t left = (listed + 1) / 2;
            for (size_t i = 0; i < max_sensors; ++i) {
                const bool is_left = i < left;
                lv_obj_set_pos(trends_.legend[i], is_left ? 2 : 64,
                               legend_top + legend_row * int(is_left ? i : i - left));
            }
        }
        // A line needs two points. Until some probe has them (the first full five-minute bucket
        // is followed by a second one five minutes later) the chart and its scale are not shown:
        // a single point has the same highest and lowest value and draws nothing.
        size_t most_points = 0;
        for (unsigned i = 0; i < max_sensors; ++i)
            if (chart_data_[i] && rows[i].rom && rows[i].state != State::disabled)
                most_points = std::max(most_points, chart_points_[i]);
        const bool collecting = low != INT32_MAX && most_points < 2;
        const bool any = low != INT32_MAX && !collecting;
        if (any && (low != trend_low_ || high != trend_high_)) {
            // The labels are the scale's own bounds and middle, not just the data's extremes.
            const Axis axis = nice_axis(low, high);
            lv_chart_set_axis_range(trends_.charts[0], LV_CHART_AXIS_PRIMARY_Y, axis.low,
                                    axis.high);
            char text_buffer[16];
            std::snprintf(text_buffer, sizeof(text_buffer), "%.1f", axis.high / 100.0);
            text(trends_.axis_max[0], text_buffer);
            std::snprintf(text_buffer, sizeof(text_buffer), "%.1f", (axis.low + axis.high) / 200.0);
            text(trends_.axis_mid, text_buffer);
            std::snprintf(text_buffer, sizeof(text_buffer), "%.1f", axis.low / 100.0);
            text(trends_.axis_min[0], text_buffer);
        }
        trend_low_ = low;
        trend_high_ = high;
        lv_obj_set_hidden(trends_.charts[0], !any);
        for (auto *widget : {trends_.axis_max[0], trends_.axis_mid, trends_.axis_min[0],
                             trends_.axis_from, trends_.axis_mid_x, trends_.axis_now})
            lv_obj_set_hidden(widget, !any);
        lv_obj_set_hidden(trends_.no_data[0], any);
        text(trends_.no_data[0], collecting ? "COLLECTING DATA" : "NO DATA");
        text(trends_.title, options.fahrenheit ? "Trends °F" : "Trends °C");
        // Whole hours read better and are shorter ("-1h" instead of "-60m").
        auto ago = [](unsigned minutes) {
            return minutes >= 60 && minutes % 60 == 0 ? "-" + std::to_string(minutes / 60) + "h"
                                                      : "-" + std::to_string(minutes) + "m";
        };
        text(trends_.axis_from, ago(options.lookback_minutes));
        text(trends_.axis_mid_x, ago(options.lookback_minutes / 2));
        text(trends_.axis_now, "now");
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
        // small screen), else nothing: the Wi-Fi state is the icon beside the time.
        overview_footer = !urgent.empty() ? urgent : !status.logging ? "Log ERR" : "";
        trends_footer = overview_footer; // the lookback is on the chart's time axis
    } else {
        overview_footer = urgent.empty() ? status_line : status_line + "\n" + urgent;
        trends_footer =
            status_line + "\n" +
            (urgent.empty() ? "Past " + std::to_string(options.lookback_minutes) + " min" : urgent);
    }
    if (compact_) {
        // Green: connected. Amber: connecting. Red: not connected (offline or setup only).
        const uint32_t wifi_color = status.wifi == "connected"    ? 0x9be3c4
                                    : status.wifi == "connecting" ? 0xffd583
                                                                  : 0xff7a7a;
        for (auto *screen : {&overview_, &trends_}) {
            text(screen->wifi, LV_SYMBOL_WIFI);
            text_color(screen->wifi, wifi_color);
        }
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
                               const std::string &owner_code, bool owner_page_requested,
                               const std::string &local_name) {
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
        // With a name to show, the compact page gives the address block more room: the heading
        // moves up and the address and name share one small-font label that wraps at hyphens.
        const bool compact_with_name = compact && !local_name.empty();
        lv_obj_set_y(setup_slots_[5], compact_with_name ? 104 : 106);
        lv_obj_set_y(setup_slots_[6], compact_with_name ? 118 : 121);
        lv_obj_set_height(setup_slots_[6], compact_with_name ? 42 : 18);
        lv_obj_set_style_text_font(
            setup_slots_[6],
            compact_with_name ? &lv_font_montserrat_12
                              : (compact ? &lv_font_montserrat_14 : &lv_font_montserrat_24),
            0);
        set(6, compact_with_name ? "192.168.4.1\nor " + local_name : "192.168.4.1");
        set(7, compact ? "" : "3  One-time owner code");
        set(8, compact ? "" : two_lines(owner_code));
        set(10, "");
        set(9, compact ? (has_owner_code && local_name.empty() ? "1/2" : "")
                       : (local_name.empty() ? "" : "or " + local_name));
        if (!compact && !has_owner_code)
            set(7, "");
    }
    if (lv_screen_active() != setup_)
        lv_screen_load(setup_);
}
} // namespace monitor

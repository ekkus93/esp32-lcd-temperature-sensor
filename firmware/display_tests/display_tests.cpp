#include "charter_fonts.h"
#include "display_view.hpp"
#include "fake_bus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <png.h>
#include <set>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    std::filesystem::create_directories(argv[1]);
    lv_init();
    auto *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    std::vector<uint8_t> framebuffer(240 * 320 * 3);
    lv_display_set_buffers(display, framebuffer.data(), nullptr, framebuffer.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(
        display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    // Concatenated text of every visible label on the active screen.
    auto visible_text = [](lv_obj_t *root) {
        std::string out;
        for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
            auto *child = lv_obj_get_child(root, i);
            if (!lv_obj_is_hidden(child) && lv_obj_check_type(child, &lv_label_class))
                out += std::string(lv_label_get_text(child)) + "\n";
        }
        return out;
    };
    auto save = [&](const std::string &name) {
        lv_tick_inc(50);
        lv_refr_now(display);
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = lv_display_get_horizontal_resolution(display);
        image.height = lv_display_get_vertical_resolution(display);
        image.format = PNG_FORMAT_RGB;
        auto path = std::string(argv[1]) + '/' + name + ".png";
        // LVGL RGB888 stores BGR bytes in memory on this little-endian host.
        std::vector<uint8_t> pixels(framebuffer.begin(),
                                    framebuffer.begin() + image.width * image.height * 3);
        for (size_t i = 0; i < pixels.size(); i += 3)
            std::swap(pixels[i], pixels[i + 2]);
        CHECK(png_image_write_to_file(&image, path.c_str(), 0, pixels.data(), 0, nullptr));
        CHECK(std::any_of(pixels.begin(), pixels.end(), [](uint8_t p) { return p > 220; }));
    };
    auto *blank = lv_screen_active();
    {
        DisplayView view;
        DisplayOptions options;
        DisplayStatus status;
        TrendCache cache;
        Registry registry;
        view.render(registry, cache, options, status, 0);
        CHECK(!view.showing_trends());
        save("overview-empty");
        unsigned invalidations = 0;
        auto *stable_event = lv_display_add_event_cb(
            display,
            [](lv_event_t *event) { ++*static_cast<unsigned *>(lv_event_get_user_data(event)); },
            LV_EVENT_REFR_REQUEST, &invalidations);
        view.render(registry, cache, options, status, 0);
        CHECK(invalidations == 0);
        lv_display_remove_event(display, lv_display_get_event_count(display) - 1);
        (void)stable_event;
        for (unsigned count = 1; count <= 5; ++count) {
            registry = Registry{};
            registry.sample(fake_readings(count), 0);
            view.render(registry, cache, options, status, 0);
            save("overview-" + std::to_string(count));
            // The large Overview has no lookback window to describe: no "Past N min" there.
            CHECK(visible_text(lv_screen_active()).find("Past") == std::string::npos);
        }
        {
            // DS18B20 extremes must fit the same 7-character value field in both scales.
            auto extremes = fake_readings(2);
            *extremes[0].celsius = 125;
            *extremes[1].celsius = -55;
            Registry extreme_registry;
            extreme_registry.sample(extremes, 0);
            const auto fahrenheit_rows = display_rows(extreme_registry, true);
            CHECK(fahrenheit_rows[0].alt_value == "125.0\xC2\xB0"
                                                  "C");
            CHECK(fahrenheit_rows[1].alt_value == "-55.0\xC2\xB0"
                                                  "C");
            CHECK(fahrenheit_rows[2].alt_value.empty()); // unused slot has no second unit
            CHECK(fahrenheit_rows[0].value == "257.0\xC2\xB0"
                                              "F");
            CHECK(fahrenheit_rows[1].value == "-67.0\xC2\xB0"
                                              "F");
            const auto celsius_rows = display_rows(extreme_registry, false);
            CHECK(celsius_rows[0].alt_value == "257.0\xC2\xB0"
                                               "F");
            CHECK(celsius_rows[1].alt_value == "-67.0\xC2\xB0"
                                               "F");
            CHECK(celsius_rows[0].value == "125.0\xC2\xB0"
                                           "C");
            CHECK(celsius_rows[1].value == "-55.0\xC2\xB0"
                                           "C");
        }
        auto sensors = registry.sensors();
        for (unsigned i = 0; i < sensors.size(); ++i) {
            auto settings = sensors[i].settings;
            settings.name = "Longest permitted sensor label ABC";
            settings.name.resize(32);
            settings.calibration = -calibration_limit_c;
            settings.high = -10;
            settings.persistence_ms = 0;
            CHECK(registry.configure(sensors[i].rom, settings));
        }
        registry.sample(fake_readings(), 5000);
        for (const auto &row : display_rows(registry, false))
            CHECK(row.status == "! HIGH");
        view.render(registry, cache, options, status, 5000);
        save("overview-five-warnings-long-names");
        auto cold = fake_readings();
        for (auto &reading : cold)
            reading.celsius = -55;
        registry.sample(cold, 6000);
        status.local_time = "23:59";
        status.wifi = "connected";
        status.logging = true;
        CHECK(options.fahrenheit); // default scale
        view.render(registry, cache, options, status, 6000);
        save("overview-negative-fahrenheit");
        options.fahrenheit = false;
        view.render(registry, cache, options, status, 6000);
        save("overview-negative-celsius");
        options.fahrenheit = true;
        status.local_time.clear();
        registry.sample(fake_readings(), 10000);
        view.render(registry, cache, options, status, 10000);
        lv_tick_inc(50);
        lv_refr_now(display);
        const auto previous_live_frame = framebuffer;
        auto updated_readings = fake_readings();
        *updated_readings[0].celsius += 5;
        registry.sample(updated_readings, 15000);
        view.render(registry, cache, options, status, 15000);
        lv_tick_inc(50);
        lv_refr_now(display);
        CHECK(framebuffer != previous_live_frame);
        save("overview-live-update-without-history");
        status.wifi = "offline";
        status.local_time.clear();
        view.render(registry, cache, options, status, 16000);
        lv_tick_inc(50);
        lv_refr_now(display);
        const auto offline_frame = framebuffer;
        status.wifi = "connecting";
        view.render(registry, cache, options, status, 17000);
        lv_tick_inc(50);
        lv_refr_now(display);
        CHECK(framebuffer != offline_frame);
        const auto connecting_frame = framebuffer;
        status.wifi = "connected";
        status.local_time = "23:59";
        view.render(registry, cache, options, status, 18000);
        lv_tick_inc(50);
        lv_refr_now(display);
        CHECK(framebuffer != connecting_frame);
        save("overview-wifi-clock-restored");
        // The read-only LCD must expose OTA verification/rollback status
        // without adding a third normal view or masking the Wi-Fi/logging line.
        auto footer_text = [&] { return std::string(lv_label_get_text(view.footer_label())); };
        for (const char *state : {"new", "pending_verify"}) {
            status.ota_state = state;
            view.render(registry, cache, options, status, 19000);
            CHECK(footer_text().find("OTA VERIFYING") != std::string::npos);
            CHECK(footer_text().find("Wi-Fi connected | Log OK") != std::string::npos);
        }
        save("overview-ota-verifying");
        for (const char *state : {"invalid", "aborted"}) {
            status.ota_state = state;
            view.render(registry, cache, options, status, 20000);
            CHECK(footer_text().find("OTA ROLLBACK") != std::string::npos);
        }
        status.ota_state = "valid";
        view.render(registry, cache, options, status, 22000);
        CHECK(footer_text().find("OTA ") == std::string::npos);
        options.extended = true;
        view.render(registry, cache, options, status, 5000);
        status.ota_state = "pending_verify";
        view.render(registry, cache, options, status, 25000);
        CHECK(view.showing_trends());
        CHECK(footer_text().find("OTA VERIFYING") != std::string::npos);
        status.ota_state.clear();
        view.render(registry, cache, options, status, 25000);
        save("trends-no-data");
        for (int64_t t = 0; t < 3600000; t += bucket_ms) {
            Bucket bucket{123, t, {}, {}};
            for (unsigned i = 0; i < sensors.size(); ++i)
                if (t != 1800000)
                    bucket.sensors.push_back(
                        {sensors[i].rom, -12.0 + double(t / bucket_ms) + i, -15, 10, 60, false});
            cache.add(bucket);
        }
        auto values = cache.values(sensors[0].rom, 3600000, 60);
        CHECK(values.size() == 12 && !values[6] && values[5]);
        view.render(registry, cache, options, status, 3625000);
        CHECK(view.showing_trends());
        save("trends-negative-with-gap");
        registry.sample({}, 3630000, true);
        options.extended = false;
        view.render(registry, cache, options, status, 3630000);
        save("overview-bus-fault");
        auto disabled = registry.sensors()[0].settings;
        disabled.enabled = false;
        CHECK(registry.configure(sensors[0].rom, disabled));
        registry.sample({}, 3635000);
        registry.sample({}, 3640000);
        registry.sample({}, 3645000);
        status.button_progress = 75;
        view.render(registry, cache, options, status, 3645000);
        CHECK(footer_text().find("Hold BOOT 75%") != std::string::npos);
        save("overview-off-and-lost");
        CHECK(display_rows(registry, false)[0].value == "OFF");
        CHECK(display_rows(registry, false)[0].alt_value.empty()); // no reading, no second unit
        for (const auto &row : display_rows(registry, false))
            CHECK(row.value == "OFF" || row.value == "--.-");
        view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD");
        save("provisioning-simulation-only");
        {
            const auto text = visible_text(lv_screen_active());
            for (const char *expected :
                 {"Wi-Fi setup", "1  Join this Wi-Fi", "TempMonitor-A1B2C3", "Password",
                  "K7M4Q9XP2WTD", "2  Open in a browser", "192.168.4.1"})
                CHECK(text.find(expected) != std::string::npos);
        }
        // Once the device is also on the home network, its name is offered as an alternative.
        view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD", "temperature-monitor.local");
        save("provisioning-simulation-only-local-name");
        CHECK(visible_text(lv_screen_active()).find("or temperature-monitor.local") !=
              std::string::npos);
        CHECK(visible_text(lv_screen_active()).find("192.168.4.1") != std::string::npos);
        view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD");
        save("provisioning-enrolled-simulation-only");
        options.extended = false;
        view.render(registry, cache, options, status, 3650000);
        CHECK(!view.showing_trends());
        lv_screen_load(blank);
    }
    lv_display_set_resolution(display, 128, 160);
    {
        DisplayView view;
        DisplayOptions options;
        DisplayStatus status;
        TrendCache cache;
        Registry registry;
        registry.sample(fake_readings(), 0);
        view.render(registry, cache, options, status, 0);
        CHECK(lv_obj_get_width(lv_screen_active()) == 128);
        CHECK(lv_obj_get_height(lv_screen_active()) == 160);
        lv_tick_inc(50);
        lv_refr_now(display);
        for (int64_t t = 0; t < 3600000; t += bucket_ms) {
            Bucket bucket{123, t, {}, {}};
            for (const auto &sensor : registry.sensors())
                bucket.sensors.push_back(
                    {sensor.rom, 20.0 + double(t / bucket_ms), 18, 34, 60, false});
            cache.add(bucket);
        }
        {
            // Compact Overview in the default Fahrenheit scale at the DS18B20 extremes.
            auto readings = fake_readings(5);
            const double celsius_values[] = {125.0, -55.0, 37.8, 18.0625, 0.0};
            for (size_t i = 0; i < 5; ++i)
                readings[i].celsius = celsius_values[i];
            Registry compact_registry;
            compact_registry.sample(readings, 0);
            CHECK(options.fahrenheit && !options.extended);
            view.render(compact_registry, cache, options, status, 1000);
            lv_tick_inc(50);
            lv_refr_now(display);
            save("overview-compact-fahrenheit");
            {
                // Each reading shows in both scales on the compact Overview.
                const auto both = visible_text(lv_screen_active());
                for (const char *expected : {"257.0\xC2\xB0"
                                             "F",
                                             "125.0\xC2\xB0"
                                             "C",
                                             "-67.0\xC2\xB0"
                                             "F",
                                             "-55.0\xC2\xB0"
                                             "C",
                                             "64.5\xC2\xB0"
                                             "F",
                                             "18.1\xC2\xB0"
                                             "C"})
                    CHECK(both.find(expected) != std::string::npos);
            }
            options.fahrenheit = false;
            view.render(compact_registry, cache, options, status, 1000);
            lv_tick_inc(50);
            lv_refr_now(display);
            save("overview-compact-celsius");
            {
                const auto both = visible_text(lv_screen_active());
                CHECK(both.find("125.0\xC2\xB0"
                                "C") != std::string::npos);
                CHECK(both.find("257.0\xC2\xB0"
                                "F") != std::string::npos);
            }
            options.fahrenheit = true;
        }
        {
            // Compact footer priority: BOOT hold, bus fault, OTA state, logging failure, then the
            // Wi-Fi state. A storage failure must be visible on the small screen, and a BOOT
            // hold must stay visible during a bus fault.
            Registry faulty;
            faulty.sample(fake_readings(2), 1000, true);
            CHECK(faulty.bus_fault());
            Registry healthy_registry;
            healthy_registry.sample(fake_readings(2), 1000);
            DisplayStatus footer_status;
            footer_status.wifi = "connected";
            footer_status.logging = true;
            auto footer_of = [&](const Registry &r, const DisplayStatus &st) {
                view.render(r, cache, options, st, 1000);
                lv_tick_inc(50);
                lv_refr_now(display);
                return visible_text(lv_screen_active());
            };
            auto has = [](const std::string &text, const char *needle) {
                return text.find(needle) != std::string::npos;
            };
            auto text = footer_of(healthy_registry, footer_status);
            // Wi-Fi is an icon beside the time now, not footer text.
            auto wifi_color = [&] {
                return lv_color_to_u32(
                           lv_obj_get_style_text_color(view.wifi_icon(), LV_PART_MAIN)) &
                       0xffffff;
            };
            CHECK(!has(text, "Wi-Fi") && !has(text, "Log ERR") && has(text, LV_SYMBOL_WIFI));
            CHECK(wifi_color() == 0x9be3c4); // connected
            footer_status.wifi = "connecting";
            footer_of(healthy_registry, footer_status);
            CHECK(wifi_color() == 0xffd583);
            footer_status.wifi = "offline";
            footer_of(healthy_registry, footer_status);
            CHECK(wifi_color() == 0xff7a7a);
            save("overview-compact-wifi-icon-offline");
            footer_status.wifi = "connected";
            footer_status.logging = false;
            text = footer_of(healthy_registry, footer_status);
            CHECK(has(text, "Log ERR") && !has(text, "Wi-Fi"));
            // Each faulty sensor row also says BUS FAULT, so the footer's own text is found by
            // counting occurrences.
            auto count = [](const std::string &haystack, const std::string &needle) {
                size_t n = 0;
                for (size_t at = haystack.find(needle); at != std::string::npos;
                     at = haystack.find(needle, at + 1))
                    ++n;
                return n;
            };
            text = footer_of(faulty, footer_status); // bus fault outranks a logging failure
            const auto fault_with_footer = count(text, "BUS FAULT");
            CHECK(fault_with_footer >= 1 && !has(text, "Log ERR"));
            footer_status.button_progress = 60; // a BOOT hold outranks the bus fault
            text = footer_of(faulty, footer_status);
            CHECK(has(text, "Hold BOOT 60%"));
            CHECK(count(text, "BUS FAULT") + 1 == fault_with_footer); // the footer gave way
            footer_status.button_progress = 0;
            footer_status.logging = true;
            footer_status.ota_state = "pending_verify";
            text = footer_of(healthy_registry, footer_status);
            CHECK(has(text, "OTA VERIFYING"));
            footer_status.ota_state.clear();
            text = footer_of(healthy_registry, footer_status);
            CHECK(!has(text, "Wi-Fi") && !has(text, "OTA") && !has(text, "Log ERR"));
        }
        options.extended = true;
        options.overview_seconds = 5;
        options.trends_seconds = 10;
        view.render(registry, cache, options, status, 3600000);
        view.render(registry, cache, options, status, 3605000);
        CHECK(view.showing_trends());
        // Every probe that exists has a legend entry in its own colour, and there is one chart.
        auto legend_entries = [&]() {
            unsigned shown = 0;
            for (unsigned i = 0; i < max_sensors; ++i)
                shown += !lv_obj_is_hidden(view.trends_legend(i));
            return shown;
        };
        CHECK(legend_entries() == 5);
        CHECK(!lv_obj_is_hidden(view.trends_chart(0)));
        CHECK(lv_obj_is_hidden(view.trends_no_data()));
        for (unsigned i = 0; i < max_sensors; ++i)
            CHECK(lv_obj_is_hidden(view.trends_label(i))); // no per-probe header any more
        {
            // Five probes: three names on the left, two on the right, all below the chart and
            // above the footer.
            lv_obj_update_layout(lv_screen_active());
            for (unsigned i = 0; i < 3; ++i)
                CHECK(lv_obj_get_x(view.trends_legend(i)) == lv_obj_get_x(view.trends_legend(0)));
            for (unsigned i = 3; i < 5; ++i)
                CHECK(lv_obj_get_x(view.trends_legend(i)) == lv_obj_get_x(view.trends_legend(3)) &&
                      lv_obj_get_x(view.trends_legend(3)) > lv_obj_get_x(view.trends_legend(0)));
            CHECK(lv_obj_get_y(view.trends_legend(1)) > lv_obj_get_y(view.trends_legend(0)));
            CHECK(lv_obj_get_y(view.trends_legend(3)) == lv_obj_get_y(view.trends_legend(0)));
            CHECK(lv_obj_get_y(view.trends_legend(4)) == lv_obj_get_y(view.trends_legend(1)));
            CHECK(lv_obj_get_y(view.trends_legend(2)) + lv_obj_get_height(view.trends_legend(2)) <=
                  lv_obj_get_y(view.footer_label()));
            CHECK(lv_obj_get_y(view.trends_legend(0)) >=
                  lv_obj_get_y(view.trends_chart(0)) + lv_obj_get_height(view.trends_chart(0)));
        }
        {
            // Repeated renders of unchanged data while Trends is shown must not repaint.
            unsigned trend_invalidations = 0;
            lv_display_add_event_cb(
                display,
                [](lv_event_t *event) {
                    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
                },
                LV_EVENT_REFR_REQUEST, &trend_invalidations);
            lv_refr_now(display);
            trend_invalidations = 0;
            for (int64_t t = 3605100; t <= 3606000; t += 100) {
                view.render(registry, cache, options, status, t);
                lv_tick_inc(100); // let the refresh timer run, as the board's 100 ms task does
                lv_refr_now(display);
            }
            CHECK(view.showing_trends());
            CHECK(trend_invalidations == 0);
            lv_display_remove_event(display, lv_display_get_event_count(display) - 1);
        }
        {
            // A healthy probe with no trend history yet shows NO DATA. That text must be set once,
            // not flipped between blank and NO DATA on every 100 ms refresh.
            TrendCache no_history;
            Registry healthy;
            healthy.sample(fake_readings(1), 3606000);
            CHECK(display_rows(healthy, true)[0].status.empty());
            unsigned no_data_invalidations = 0;
            lv_display_add_event_cb(
                display,
                [](lv_event_t *event) {
                    ++*static_cast<unsigned *>(lv_event_get_user_data(event));
                },
                LV_EVENT_REFR_REQUEST, &no_data_invalidations);
            view.render(healthy, no_history, options, status, 3606000);
            lv_refr_now(display);
            CHECK(view.showing_trends());
            // With no history yet the probe is still listed in its own line colour (sky blue).
            CHECK(!lv_obj_is_hidden(view.trends_legend(0)));
            CHECK(
                (lv_color_to_u32(lv_obj_get_style_text_color(view.trends_legend(0), LV_PART_MAIN)) &
                 0xffffff) == 0x7fc8ff);
            no_data_invalidations = 0;
            lv_obj_invalidate(lv_screen_active()); // the counter must be able to see repaints
            CHECK(no_data_invalidations > 0);
            lv_tick_inc(100);
            lv_refr_now(display);
            no_data_invalidations = 0;
            for (int64_t t = 3606100; t <= 3607000; t += 100) {
                view.render(healthy, no_history, options, status, t);
                lv_tick_inc(100);
                lv_refr_now(display);
            }
            CHECK(view.showing_trends());
            CHECK(no_data_invalidations == 0);
            lv_display_remove_event(display, lv_display_get_event_count(display) - 1);
            view.render(registry, cache, options, status, 3607100);
        }
        lv_obj_update_layout(lv_screen_active());
        auto *first_chart = view.trends_chart(0);
        CHECK(lv_obj_get_width(first_chart) == 86 && lv_obj_get_height(first_chart) == 64);
        {
            // Fonts by role: serif (Charter) for every piece of text on the Overview and Trends
            // screens; the Wi-Fi icon is a symbol and stays in Montserrat.
            auto font_of = [](lv_obj_t *obj) {
                return lv_obj_get_style_text_font(obj, LV_PART_MAIN);
            };
            CHECK(font_of(view.trends_legend(0)) == &font_charter_regular_12);
            CHECK(font_of(view.footer_label()) == &font_charter_regular_12);
            CHECK(font_of(view.trends_no_data()) == &font_charter_regular_12);
            CHECK(font_of(view.wifi_icon()) == &lv_font_montserrat_12); // a symbol, not text
        }
        {
            // The compact graph says what it shows: the unit, the highest and lowest value, and
            // the time span.
            const auto text = visible_text(lv_screen_active());
            for (const char *expected : {"Trends °F", "-1h", "-30m", "now"})
                CHECK(text.find(expected) != std::string::npos);
            CHECK(!lv_obj_is_hidden(view.trends_axis_max(0)));
        }
        save("trends-compact-all-probes-one-chart");
        {
            // Five probes with their own histories: five lines on one chart, one shared scale.
            Registry five;
            five.sample(fake_readings(5), 4200000);
            TrendCache histories;
            // Later than the other fixtures, so the history window has moved on (as it does in
            // real time whenever a bucket closes) and the chart is redrawn.
            for (int64_t t = 0; t < 4200000; t += bucket_ms) {
                Bucket bucket{123, t, {}, {}};
                const double k = double(t / bucket_ms);
                for (size_t i = 0; i < five.sensors().size(); ++i)
                    bucket.sensors.push_back(
                        {five.sensors()[i].rom,
                         12.0 + 3.0 * double(i) + 2.5 * std::sin(k / 2.0 + double(i)), 10, 30, 60,
                         false});
                histories.add(bucket);
            }
            // Extended mode alternates; step forward until the Trends screen is the one showing.
            int64_t t = 4210000;
            view.render(five, histories, options, status, t);
            for (; t < 4300000 && !view.showing_trends(); t += 1000)
                view.render(five, histories, options, status, t);
            CHECK(view.showing_trends());
            CHECK(legend_entries() == 5 && !lv_obj_is_hidden(view.trends_chart(0)));
            lv_tick_inc(50);
            lv_refr_now(display);
            save("trends-compact-five-lines");
            {
                // A real axis: the scale's top, middle and bottom, in order, containing the data
                // (the fixture spans about 49.1 to 79.6 F) with a round middle.
                auto number = [](lv_obj_t *obj) { return std::atof(lv_label_get_text(obj)); };
                const double top = number(view.trends_axis_max(0));
                const double mid = number(view.trends_axis_mid());
                const double bottom = number(view.trends_axis_min());
                CHECK(top > mid && mid > bottom);
                CHECK(top >= 79.6 && bottom <= 49.1);
                CHECK(std::fabs(mid - (top + bottom) / 2) < 0.06);
                CHECK(!lv_obj_is_hidden(view.trends_axis_mid()));
            }
            {
                // One full bucket is one point: no line, so no chart and no scale (it would show
                // the same value as both highest and lowest); the screen says it is collecting.
                TrendCache one_point;
                Bucket only{123, 3900000, {}, {}};
                for (const auto &sensor : five.sensors())
                    only.sensors.push_back({sensor.rom, 21.5, 21, 22, 60, false});
                one_point.add(only);
                int64_t t = 4210000;
                view.render(five, one_point, options, status, t);
                for (; t < 4300000 && !view.showing_trends(); t += 1000)
                    view.render(five, one_point, options, status, t);
                CHECK(view.showing_trends());
                CHECK(lv_obj_is_hidden(view.trends_chart(0)));
                CHECK(lv_obj_is_hidden(view.trends_axis_max(0)));
                CHECK(!lv_obj_is_hidden(view.trends_no_data()));
                CHECK(std::string(lv_label_get_text(view.trends_no_data())) == "COLLECTING DATA");
                save("trends-compact-collecting");
                // A second point later makes a line: the chart and its scale appear.
                Bucket next{123, 4200000, {}, {}};
                for (const auto &sensor : five.sensors())
                    next.sensors.push_back({sensor.rom, 22.0, 21, 23, 60, false});
                one_point.add(next);
                view.render(five, one_point, options, status, 4510000);
                for (t = 4510000; t < 4620000 && !view.showing_trends(); t += 1000)
                    view.render(five, one_point, options, status, t);
                CHECK(view.showing_trends());
                CHECK(!lv_obj_is_hidden(view.trends_chart(0)));
                CHECK(!lv_obj_is_hidden(view.trends_axis_max(0)));
                CHECK(lv_obj_is_hidden(view.trends_no_data()));
                // A steady reading (71.0 to 71.6 F) still gets a scale at least 2 degrees tall.
                const double span = std::atof(lv_label_get_text(view.trends_axis_max(0))) -
                                    std::atof(lv_label_get_text(view.trends_axis_min()));
                CHECK(span >= 2.0);
                CHECK(std::atof(lv_label_get_text(view.trends_axis_min())) <= 71.0 &&
                      std::atof(lv_label_get_text(view.trends_axis_max(0))) >= 71.6);
            }
            {
                // The same balance for four probes (2 + 2), three (2 + 1) and one.
                auto layout = [&](size_t count) {
                    Registry some;
                    some.sample(fake_readings(count), 4200000);
                    view.render(some, histories, options, status, 4210000);
                    lv_obj_update_layout(lv_screen_active());
                    std::array<int, 5> column{}; // 0 = left, 1 = right, -1 = hidden
                    for (size_t i = 0; i < 5; ++i)
                        column[i] = lv_obj_is_hidden(view.trends_legend(i))
                                        ? -1
                                        : (lv_obj_get_x(view.trends_legend(i)) >
                                                   lv_obj_get_x(view.trends_legend(0))
                                               ? 1
                                               : 0);
                    return column;
                };
                CHECK((layout(4) == std::array<int, 5>{0, 0, 1, 1, -1}));
                CHECK((layout(3) == std::array<int, 5>{0, 0, 1, -1, -1}));
                CHECK((layout(1) == std::array<int, 5>{0, -1, -1, -1, -1}));
                CHECK((layout(5) == std::array<int, 5>{0, 0, 0, 1, 1}));
            }
            view.render(registry, cache, options, status, 3605000);
        }
        {
            // Each probe's line has its own colour, and the legend shows the same colours.
            std::set<uint32_t> colours;
            for (unsigned i = 0; i < max_sensors; ++i)
                colours.insert(lv_color_to_u32(lv_obj_get_style_text_color(view.trends_legend(i),
                                                                           LV_PART_MAIN)) &
                               0xffffff);
            CHECK(colours.size() == max_sensors);
            // Nothing rotates any more: the same screen over time.
            const auto first = visible_text(lv_screen_active());
            for (int64_t t = 3610000; t <= 3730000; t += 10000) {
                view.render(registry, cache, options, status, t);
                if (view.showing_trends())
                    CHECK(legend_entries() == 5 && visible_text(lv_screen_active()) == first);
            }
        }
        Registry empty;
        view.render(empty, cache, options, status, 3730000);
        CHECK(view.showing_trends());
        CHECK(lv_obj_is_hidden(view.trends_chart(0)));
        CHECK(!lv_obj_is_hidden(view.trends_no_data()));
        CHECK(legend_entries() == 0);
        view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD");
        const auto first_page = visible_text(lv_screen_active());
        save("provisioning-compact-wifi");
        {
            // With a name to offer, the compact page shows it under the setup address.
            view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD", "temperature-monitor.local");
            const auto named = visible_text(lv_screen_active());
            save("provisioning-compact-wifi-local-name");
            for (const char *expected : {"192.168.4.1", "or temperature-monitor.local",
                                         "2  Open in a browser", "K7M4Q9XP2WTD"})
                CHECK(named.find(expected) != std::string::npos);
            // A short name fits on one line as well.
            view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD", "freezer.local");
            save("provisioning-compact-wifi-local-name-short");
            CHECK(visible_text(lv_screen_active()).find("or freezer.local") != std::string::npos);
            // Without a name the page returns to its normal layout, page marker included.
            view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD");
            const auto plain = visible_text(lv_screen_active());
            CHECK(plain.find("or ") == std::string::npos);
        }
        for (const char *expected : {"1  Join this Wi-Fi", "TempMonitor-\nA1B2C3", "K7M4Q9XP2WTD",
                                     "2  Open in a browser", "192.168.4.1"})
            CHECK(first_page.find(expected) != std::string::npos);
        {
            // Each line of the compact SSID must fit the 124 px label for every hex suffix.
            auto *probe = lv_label_create(lv_screen_active());
            lv_obj_set_style_text_font(probe, &lv_font_montserrat_12, 0);
            lv_obj_set_width(probe, LV_SIZE_CONTENT);
            int32_t widest = 0;
            for (char digit : std::string("0123456789ABCDEF"))
                for (const auto &line : {std::string("TempMonitor-"), std::string(6, digit)}) {
                    lv_label_set_text(probe, line.c_str());
                    lv_obj_update_layout(probe);
                    widest = std::max(widest, lv_obj_get_width(probe));
                }
            lv_obj_delete(probe);
            CHECK(widest <= 124);
        }
        // The setup page does not change while it is shown: there is no second page to alternate
        // to.
        view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD");
        const auto steady_first = visible_text(lv_screen_active());
        save("provisioning-compact-steady");
        lv_tick_inc(10000);
        view.provisioning("TempMonitor-A1B2C3", "K7M4Q9XP2WTD");
        CHECK(visible_text(lv_screen_active()) == steady_first);
        CHECK(steady_first.find("K7M4Q9XP2WTD") != std::string::npos);
        lv_screen_load(blank);
    }
    {
        TrendCache cache;
        for (unsigned j = 0; j < 300; ++j)
            cache.add({123, int64_t(j) * bucket_ms, {}, {}});
        CHECK(cache.size() == 288);
    }
    lv_display_delete(display);
    lv_deinit();
    std::cout << "PASS: headless LVGL Overview/Trends, 0-5 probes, warnings, gaps, bounded cache, "
                 "BOOT feedback, OTA status and provisioning\n";
}

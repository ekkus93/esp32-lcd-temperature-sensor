#include "display_view.hpp"
#include "fake_bus.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <png.h>
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
    auto save = [&](const std::string &name) {
        lv_tick_inc(50);
        lv_refr_now(display);
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = 240;
        image.height = 320;
        image.format = PNG_FORMAT_RGB;
        auto path = std::string(argv[1]) + '/' + name + ".png";
        // LVGL RGB888 stores BGR bytes in memory on this little-endian host.
        auto pixels = framebuffer;
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
        for (unsigned count = 1; count <= 5; ++count) {
            registry = Registry{};
            registry.sample(fake_readings(count), 0);
            view.render(registry, cache, options, status, 0);
            save("overview-" + std::to_string(count));
        }
        auto sensors = registry.sensors();
        for (unsigned i = 0; i < sensors.size(); ++i) {
            auto settings = sensors[i].settings;
            settings.name = "Longest permitted sensor label ABC";
            settings.name.resize(32);
            settings.calibration = -20;
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
        options.fahrenheit = true;
        view.render(registry, cache, options, status, 6000);
        save("overview-negative-fahrenheit");
        options.fahrenheit = false;
        status.local_time.clear();
        registry.sample(fake_readings(), 10000);
        options.extended = true;
        view.render(registry, cache, options, status, 5000);
        view.render(registry, cache, options, status, 25000);
        CHECK(view.showing_trends());
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
        save("overview-off-and-lost");
        CHECK(display_rows(registry, false)[0].value == "OFF");
        for (const auto &row : display_rows(registry, false))
            CHECK(row.value == "OFF" || row.value == "--.-");
        view.provisioning("SIMULATION-ONLY", "example-only-wifi-key", "example-only-owner-code");
        save("provisioning-simulation-only");
        options.extended = false;
        view.render(registry, cache, options, status, 3650000);
        CHECK(!view.showing_trends());
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
                 "BOOT feedback and provisioning\n";
}

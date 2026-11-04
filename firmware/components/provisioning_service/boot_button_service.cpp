#include "boot_button_service.hpp"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "monitor.hpp"
#include <atomic>

namespace monitor {
namespace {
std::atomic<unsigned> hold_progress{0};

void button_task(void *context) {
    auto &wifi = *static_cast<WifiService *>(context);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << PIN_BOOT;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&config);
    BootButton button;
    while (true) {
        const int64_t now = esp_timer_get_time() / 1000;
        const bool provision =
            button.update(gpio_get_level(static_cast<gpio_num_t>(PIN_BOOT)) == 0, now);
        hold_progress.store(button.progress(now));
        if (provision)
            wifi.provision();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
} // namespace

bool start_boot_button_service(WifiService &wifi) {
    return xTaskCreate(button_task, "boot_button", 3072, &wifi, 2, nullptr) == pdPASS;
}

unsigned boot_hold_progress() {
    return hold_progress.load();
}
} // namespace monitor

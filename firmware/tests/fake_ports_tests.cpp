// The HTTP API ports build on the host and their recording fakes count every call and every
// device-changing call (God file refactor G02.2).
#include "fake_exchange.hpp"
#include "fake_ports.hpp"
#include <cstdlib>
#include <iostream>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int main() {
    FakeDevice device;
    // Through the port interfaces, as routes see them.
    AuthPort &auth = device.auth;
    NetworkPort &network = device.network;
    HistoryPort &history = device.history;
    DeviceSettingsPort &settings = device.settings;
    SystemPort &system = device.system;
    SensorUpdates &updates = device.updates;
    LiveState &live = device.live;

    // Reads change nothing.
    CHECK(auth.enrolled());
    CHECK(!auth.authorize("nobody", 0));
    CHECK(network.info().state == "connected" && !network.candidate_busy());
    std::vector<HistoryPoint> points;
    std::vector<HistoryGap> gaps;
    bool truncated = true;
    CHECK(history.query({}, points, gaps, truncated) && !truncated);
    CHECK(history.storage_info().available);
    DeviceOptions options;
    uint64_t revision = 0;
    bool writable = false;
    CHECK(settings.get(options, revision, writable) && revision == 1 && writable);
    CHECK(system.ota_status().state == "valid" && system.reboot_available());
    CHECK(updates.sensor_settings_writable() && !updates.update_pending(0));
    LiveView view;
    CHECK(live.read(view) && live.boot_id() == "0123456789abcdef" && !live.simulated());
    CHECK(device.side_effects() == 0);

    // A signed-in session is recognised only with its token.
    device.auth.session = Session{"token", "csrf", 3600000};
    CHECK(auth.authorize("token", 0) && !auth.authorize("other", 0));
    CHECK(device.auth.log.count("authorize") == 3);

    // Every device-changing call is counted once.
    auth.logout("token");
    CHECK(auth.change_password("token", "old", "new password", 0));
    CHECK(network.connect("Home", "secret") && network.provision() && network.scan());
    CHECK(network.mdns_rename("garage"));
    CHECK(settings.save(options, 1) == ESP_OK && device.settings.revision == 2);
    CHECK(system.schedule_reboot() && system.schedule_factory_reset(true));
    system.restart_now();
    FakeExchange upload;
    CHECK(system.upload_firmware(upload) == ESP_OK && upload.status == "202 Accepted");
    CHECK(updates.queue_sensor_update(1, Settings{}, false) == QueueResult::queued);
    CHECK(updates.queue_calibration_hold(1, 60000, false) == QueueResult::queued);
    CHECK(auth.enroll("a long password", 0, true) == EnrollResult::enrolled);
    CHECK(device.side_effects() == 14);
    CHECK(device.system.reset_erase_history == true);

    // Refused queue writes are not side effects.
    device.updates.result = QueueResult::full;
    CHECK(updates.queue_sensor_update(1, Settings{}, true) == QueueResult::full);
    CHECK(device.side_effects() == 14);
    // A busy published state reads as unavailable.
    device.live.busy = true;
    CHECK(!live.read(view));
    std::cout << "HTTP API ports build on the host; fakes record every call\n";
}

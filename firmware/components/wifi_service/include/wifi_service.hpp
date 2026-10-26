#pragma once
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <atomic>
#include <string>
#include <vector>
namespace monitor {
struct NetworkInfo {
    std::string state;
    std::string ssid;
    std::string ip;
    bool provisioning;
    std::string attempt;
    unsigned remaining_seconds;
    bool saved_credentials;
    std::string error;
};
struct ScannedNetwork {
    std::string ssid;
    int rssi;
    unsigned channel;
    bool secure;
};
class WifiService {
  public:
    esp_err_t start();
    bool connect(const std::string &ssid, const std::string &password);
    bool provision();
    bool candidate_busy() const {
        return candidate_pending_.load();
    }
    NetworkInfo info() const;
    bool scan();
    bool scan_results(std::vector<ScannedNetwork> &networks, bool &scanning);
    const std::string &ap_ssid() const {
        return ap_ssid_;
    }
    const std::string &ap_password() const {
        return ap_password_;
    }

  private:
    struct Command {
        int kind;
        char ssid[33];
        char password[65];
        uint32_t ip;
    };
    static void event(void *context, esp_event_base_t base, int32_t id, void *data);
    static void worker(void *context);
    void station(const char *ssid, const char *password);
    void set_ap(bool enabled);
    bool save(const char *ssid, const char *password);
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t scan_mutex_ = nullptr;
    std::vector<ScannedNetwork> networks_;
    std::atomic<bool> scanning_{false};
    int64_t next_scan_ = 0; // serialized HTTP caller
    std::atomic<int> state_{0};
    std::atomic<bool> ap_{false};
    std::atomic<uint32_t> ip_{0};
    std::atomic<bool> candidate_pending_{false}, saved_credentials_{false};
    std::atomic<int> attempt_{0};
    std::atomic<int> last_reason_{0};
    std::atomic<int64_t> candidate_deadline_{0};
    std::string ap_ssid_, ap_password_;
    char saved_ssid_[33]{}, saved_password_[65]{};
};
} // namespace monitor

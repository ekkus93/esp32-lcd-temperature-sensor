#include "wifi_service.hpp"
#include "auth_service.hpp"
#include "dhcpserver/dhcpserver.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "monitor.hpp"
#include "nvs.h"
#include "psa/crypto.h"
#include "wifi_ap_window.hpp"
#include "wifi_credentials.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace monitor {
namespace {
enum { got_ip = 1, disconnected = 2, candidate = 3, open_ap = 4, start_scan = 5, scan_done = 6 };
}
esp_err_t WifiService::start() {
    auto result = esp_netif_init();
    if (result != ESP_OK)
        return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        return result;
    esp_netif_create_default_wifi_sta();
    auto *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif)
        return ESP_ERR_NO_MEM;
    // Configure the offer while the new AP netif's DHCP server is still stopped.
    // Later AP-start events retain its normal automatic DHCP lifecycle.
    esp_netif_dns_info_t dns{};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("192.168.4.1");
    uint8_t offer = OFFER_DNS;
    const auto dns_option = esp_netif_dhcps_option(
        ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    const auto dns_address = esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (dns_option != ESP_OK || dns_address != ESP_OK)
        ESP_LOGW("wifi_control",
                 "AP DNS offer unavailable; direct 192.168.4.1 access remains available");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((result = esp_wifi_init(&init)) != ESP_OK)
        return result;
    esp_wifi_set_storage(WIFI_STORAGE_RAM); // Credentials commit only after verified DHCP success.
    queue_ = xQueueCreate(8, sizeof(Command));
    scan_mutex_ = xSemaphoreCreateMutex();
    if (!queue_ || !scan_mutex_)
        return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, this));
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, this));
    if ((result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK)
        return result;
    if ((result = esp_wifi_start()) != ESP_OK)
        return result;
    result = mdns_init();
    if (result != ESP_OK) {
        ESP_LOGW("wifi_control", "mDNS unavailable (%s); use the device IP",
                 esp_err_to_name(result));
    } else {
        result = mdns_hostname_set("temperature-monitor");
        if (result == ESP_OK)
            result = mdns_instance_name_set("ESP32 Temperature Monitor");
        if (result != ESP_OK) {
            ESP_LOGW("wifi_control", "mDNS hostname unavailable (%s); use the device IP",
                     esp_err_to_name(result));
            mdns_free();
        } else {
            result = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
            if (result != ESP_OK)
                ESP_LOGW("wifi_control", "mDNS HTTP service advertisement unavailable: %s",
                         esp_err_to_name(result));
            else
                ESP_LOGI("wifi_control", "mDNS available at http://temperature-monitor.local");
        }
    }
    // RF is now active, supplying hardware entropy for per-device provisioning secrets.
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char name[32];
    std::snprintf(name, sizeof(name), "TempMonitor-%02X%02X%02X", mac[3], mac[4], mac[5]);
    ap_ssid_ = name;
    nvs_handle_t handle;
    result = nvs_open("monitor_wifi", NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    char password[65]{};
    size_t size = sizeof(password);
    result = nvs_get_str(handle, "ap_pass", password, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        if (psa_crypto_init() != PSA_SUCCESS) {
            nvs_close(handle);
            return ESP_FAIL;
        }
        ap_password_ = secure_token(16);
        if (ap_password_.size() != 32) {
            nvs_close(handle);
            return ESP_FAIL;
        }
        result = nvs_set_str(handle, "ap_pass", ap_password_.c_str());
        if (result == ESP_OK)
            result = nvs_commit(handle);
    } else if (result == ESP_OK && size >= 9)
        ap_password_ = password;
    else
        result = ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) {
        nvs_close(handle);
        return result;
    }
    uint8_t record[kStationRecordV2Size]{};
    size = sizeof(record);
    const auto saved = nvs_get_blob(handle, "station1", record, &size);
    StationCredentials credentials;
    if (saved == ESP_OK && decode_station_credentials(record, size, credentials)) {
        std::memcpy(saved_ssid_, credentials.ssid.c_str(), credentials.ssid.size() + 1);
        std::memcpy(saved_password_, credentials.password.c_str(), credentials.password.size() + 1);
        saved_credentials_ = true;
    }
    // Reject malformed station records without erasing them or attempting to join with bad data.
    nvs_close(handle);
    if (xTaskCreate(worker, "wifi_control", 6144, this, 2, nullptr) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void WifiService::event(void *context, esp_event_base_t base, int32_t id, void *data) {
    auto &self = *static_cast<WifiService *>(context);
    Command command{};
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        command.kind = got_ip;
        command.ip = static_cast<ip_event_got_ip_t *>(data)->ip_info.ip.addr;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE)
        command.kind = scan_done;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        command.kind = disconnected;
        command.ip = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
    } else
        return;
    xQueueSend(self.queue_, &command, 0);
}
void WifiService::station(const char *ssid, const char *password) {
    wifi_config_t config{};
    std::memcpy(config.sta.ssid, ssid, std::strlen(ssid));
    std::memcpy(config.sta.password, password, std::strlen(password));
    esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_connect();
    state_ = 1;
}
void WifiService::set_ap(bool enabled) {
    if (enabled) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        wifi_config_t config{};
        std::memcpy(config.ap.ssid, ap_ssid_.data(), ap_ssid_.size());
        config.ap.ssid_len = ap_ssid_.size();
        std::memcpy(config.ap.password, ap_password_.data(), ap_password_.size());
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.max_connection = 4;
        config.ap.channel = 1;
        esp_wifi_set_config(WIFI_IF_AP, &config);
        ap_ = true;
    } else {
        esp_wifi_set_mode(WIFI_MODE_STA);
        ap_ = false;
    }
}
bool WifiService::save(const char *ssid, const char *password) {
    std::array<uint8_t, kStationRecordV2Size> record{};
    if (!encode_station_credentials(ssid, password, record))
        return false;
    nvs_handle_t handle;
    if (nvs_open("monitor_wifi", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto result = nvs_set_blob(handle, "station1", record.data(), record.size());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}
bool WifiService::connect(const std::string &ssid, const std::string &password) {
    if (!queue_ || !valid_station_credentials(ssid, password))
        return false;
    Command command{};
    command.kind = candidate;
    std::memcpy(command.ssid, ssid.data(), ssid.size());
    std::memcpy(command.password, password.data(), password.size());
    if (candidate_pending_.exchange(true))
        return false;
    if (xQueueSend(queue_, &command, 0) != pdTRUE) {
        candidate_pending_ = false;
        return false;
    }
    return true;
}
bool WifiService::provision() {
    Command command{};
    command.kind = open_ap;
    return queue_ && xQueueSend(queue_, &command, 0) == pdTRUE;
}
bool WifiService::scan() {
    const int64_t now = esp_timer_get_time() / 1000;
    if (!queue_ || now < next_scan_ || scanning_.exchange(true))
        return false;
    Command command{};
    command.kind = start_scan;
    if (xQueueSend(queue_, &command, 0) != pdTRUE) {
        scanning_ = false;
        return false;
    }
    next_scan_ = now + 10000;
    return true;
}
bool WifiService::scan_results(std::vector<ScannedNetwork> &networks, bool &scanning) {
    if (!scan_mutex_ || xSemaphoreTake(scan_mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return false;
    networks = networks_;
    scanning = scanning_.load();
    xSemaphoreGive(scan_mutex_);
    return true;
}
NetworkInfo WifiService::info() const {
    static const char *states[] = {"offline", "connecting", "connected"};
    esp_ip4_addr_t address{};
    address.addr = ip_.load();
    char ip[16]{};
    std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&address));
    static const char *attempts[] = {"idle", "testing", "succeeded", "failed", "storage_error"};
    const int phase = attempt_.load();
    std::string error;
    if (phase == 3) {
        const auto reason = last_reason_.load();
        if (reason == WIFI_REASON_NO_AP_FOUND ||
            reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
            reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
            reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD)
            error = "Network not found or incompatible security";
        else if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                 reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
            error = "Authentication failed; check the password";
        else
            error = "Connection timed out";
    } else if (phase == 4)
        error = "Connected, but credentials could not be saved";
    const auto remaining =
        std::max<int64_t>(0, candidate_deadline_.load() - esp_timer_get_time() / 1000);
    return {ap_ && state_ != 2 ? "setup" : states[state_],
            ap_ssid_,
            ip,
            ap_,
            attempts[phase],
            unsigned((remaining + 999) / 1000),
            saved_credentials_,
            error};
}
void WifiService::worker(void *context) {
    auto &self = *static_cast<WifiService *>(context);
    bool trying = false;
    Command proposed{};
    int64_t deadline = 0, candidate_start = 0, next_retry = 0, scan_deadline = 0;
    WifiApWindow ap_window;
    unsigned attempts = 0;
    if (self.saved_ssid_[0])
        self.station(self.saved_ssid_, self.saved_password_);
    else
        self.set_ap(true);
    while (true) {
        Command command{};
        int64_t now = esp_timer_get_time() / 1000;
        if (xQueueReceive(self.queue_, &command, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (command.kind == start_scan) {
                wifi_scan_config_t config{};
                config.show_hidden = false;
                config.scan_time.active.min = 30;
                config.scan_time.active.max = 80;
                scan_deadline = now + 10000;
                if (trying || esp_wifi_scan_start(&config, false) != ESP_OK)
                    self.scanning_ = false;
            } else if (command.kind == scan_done) {
                scan_deadline = 0;
                wifi_ap_record_t records[20]{};
                uint16_t count = 20;
                std::vector<ScannedNetwork> results;
                if (esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
                    for (unsigned i = 0; i < count; ++i) {
                        std::string ssid(reinterpret_cast<char *>(records[i].ssid),
                                         strnlen(reinterpret_cast<char *>(records[i].ssid), 32));
                        if (ssid.empty() ||
                            std::any_of(results.begin(), results.end(),
                                        [&](const ScannedNetwork &n) { return n.ssid == ssid; }))
                            continue;
                        Settings text_validation;
                        text_validation.name = ssid;
                        if (!valid_settings(text_validation))
                            continue;
                        results.push_back({ssid, records[i].rssi, records[i].primary,
                                           records[i].authmode != WIFI_AUTH_OPEN});
                    }
                }
                esp_wifi_clear_ap_list();
                xSemaphoreTake(self.scan_mutex_, portMAX_DELAY);
                self.networks_ = std::move(results);
                self.scanning_ = false;
                xSemaphoreGive(self.scan_mutex_);
            } else if (command.kind == candidate) {
                if (trying)
                    continue;
                if (self.scanning_) {
                    esp_wifi_scan_stop();
                    self.scanning_ = false;
                    scan_deadline = 0;
                }
                self.set_ap(true);
                // An earlier successful candidate may have queued AP shutdown.
                // Cancel it for this attempt so a failure leaves setup reachable.
                ap_window.candidate_started();
                proposed = command;
                trying = true;
                // Open AP+STA first, then give the HTTP handler time to return 202
                // before disconnecting a client using the current station network.
                candidate_start = now + 1000;
                deadline = now + 25000;
                self.candidate_deadline_ = deadline;
                self.attempt_ = 1;
                self.last_reason_ = 0;
            } else if (command.kind == got_ip) {
                if (trying && candidate_start)
                    continue;
                if (trying) {
                    wifi_ap_record_t current{};
                    if (esp_wifi_sta_get_ap_info(&current) != ESP_OK ||
                        std::strncmp(reinterpret_cast<const char *>(current.ssid), proposed.ssid,
                                     32) != 0)
                        continue;
                }
                self.ip_ = command.ip;
                self.state_ = 2;
                attempts = 0;
                next_retry = 0;
                if (trying) {
                    if (self.save(proposed.ssid, proposed.password)) {
                        std::memcpy(self.saved_ssid_, proposed.ssid, 33);
                        std::memcpy(self.saved_password_, proposed.password, 65);
                        self.saved_credentials_ = true;
                        self.attempt_ = 2;
                        ap_window.candidate_connected(now);
                    } else
                        self.attempt_ = 4;
                    trying = false;
                    self.candidate_pending_ = false;
                    self.candidate_deadline_ = 0;
                    candidate_start = 0;
                    std::memset(&proposed, 0, sizeof(proposed));
                }
            } else if (command.kind == disconnected) {
                if (trying)
                    self.last_reason_ = int(command.ip);
                self.state_ = 0;
                self.ip_ = 0;
                if (!trying && self.saved_ssid_[0])
                    next_retry = now + (1000LL << std::min(attempts++, 5u));
            } else if (command.kind == open_ap) {
                self.set_ap(true);
                ap_window.manual_open(now);
            }
            std::memset(&command, 0, sizeof(command));
        }
        now = esp_timer_get_time() / 1000;
        if (trying && candidate_start && now >= candidate_start) {
            candidate_start = 0;
            esp_wifi_disconnect();
            self.station(proposed.ssid, proposed.password);
        }
        if (scan_deadline && now >= scan_deadline) {
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
            self.scanning_ = false;
            scan_deadline = 0;
        }
        if (trying && now >= deadline) {
            trying = false;
            self.candidate_pending_ = false;
            self.candidate_deadline_ = 0;
            candidate_start = 0;
            self.attempt_ = 3;
            self.ip_ = 0;
            esp_wifi_disconnect();
            std::memset(&proposed, 0, sizeof(proposed));
            if (self.saved_ssid_[0])
                self.station(self.saved_ssid_, self.saved_password_);
            else
                self.state_ = 0;
        }
        if (next_retry && now >= next_retry && !trying) {
            next_retry = 0;
            self.station(self.saved_ssid_, self.saved_password_);
        }
        if (ap_window.should_close(now, trying))
            self.set_ap(false);
    }
}
} // namespace monitor

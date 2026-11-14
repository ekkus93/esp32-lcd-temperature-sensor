#pragma once
// Device implementations of the main loop's ports (God file refactor G15).
#include "device_settings_service.hpp"
#include "display_task.hpp"
#include "history_service.hpp"
#include "http_api.hpp"
#include "loop_ports.hpp"
#include "sampling_service.hpp"
#include "trend_restore_task.hpp"
#include "wifi_service.hpp"
namespace monitor {
class DeviceLoopClock final : public LoopClock {
  public:
    int64_t now_ms() override;
    std::optional<int64_t> trusted_utc() override;
    bool synchronized() override;
    std::string utc_text() override;
    std::string local_time_text() override;
};
class ApiLoopAdapter final : public LoopApi {
  public:
    explicit ApiLoopAdapter(HttpApi &api) : api_(api) {}
    bool next_sensor_update(uint64_t &rom, Settings &settings, bool &retire) override {
        return api_.next_sensor_update(rom, settings, retire);
    }
    bool next_calibration_hold(uint64_t &rom, int64_t &duration_ms, bool &cancel) override {
        return api_.next_calibration_hold(rom, duration_ms, cancel);
    }
    void set_sensor_settings_writable(bool writable) override {
        api_.set_sensor_settings_writable(writable);
    }
    bool publish(const Registry &registry, ProbeScanStatus probe_scan, int64_t uptime, bool logging,
                 bool synchronized, const std::string &utc) override {
        return api_.publish(registry, probe_scan, uptime, logging, synchronized, utc);
    }

  private:
    HttpApi &api_;
};
class SamplingLoopAdapter final : public LoopSampling {
  public:
    explicit SamplingLoopAdapter(SamplingService &sampling) : sampling_(sampling) {}
    std::vector<Reading> read(int64_t now_ms, const std::vector<uint64_t> &managed) override {
        return sampling_.read(now_ms, managed);
    }
    std::optional<ProbeMemory> store_probe_memory(uint64_t rom, ProbeMemory memory) override {
        return sampling_.store_probe_memory(rom, memory);
    }
    bool fault() const override {
        return sampling_.fault();
    }
    bool enabled() const override {
        return sampling_.enabled();
    }
    bool monitored() const override {
        return sampling_.monitored();
    }
    ProbeScanStatus scan_status() const override {
        return sampling_.scan_status();
    }

  private:
    SamplingService &sampling_;
};
class HistoryLoopAdapter final : public LoopHistory {
  public:
    explicit HistoryLoopAdapter(HistoryService &history) : history_(history) {}
    bool submit(const Bucket &bucket) override {
        return history_.submit(bucket);
    }
    bool submit_event(const JournalEvent &event) override {
        return history_.submit_event(event);
    }
    bool healthy() const override {
        return history_.healthy();
    }

  private:
    HistoryService &history_;
};
class NetworkLoopAdapter final : public LoopNetwork {
  public:
    explicit NetworkLoopAdapter(WifiService &wifi) : wifi_(wifi) {}
    LoopNetworkInfo info() const override {
        const auto network = wifi_.info();
        return {network.state, network.provisioning};
    }
    std::string mdns_hostname() const override {
        return wifi_.mdns_hostname();
    }
    std::string ap_ssid() const override {
        return wifi_.ap_ssid();
    }
    std::string ap_password() const override {
        return wifi_.ap_password();
    }

  private:
    WifiService &wifi_;
};
// Hands frames to the display task, or draws directly when that task could not start.
class DisplayLoopAdapter final : public LoopDisplay {
  public:
    DisplayLoopAdapter(DisplayRuntime &display, DisplayTaskContext &context, bool task_started,
                       DeviceSettingsService &settings)
        : display_(display), context_(context), task_started_(task_started), settings_(settings) {}
    bool ready() const override {
        return display_.ready();
    }
    bool hand_over(DisplayFrame &&frame) override;
    DeviceOptions options() override;
    unsigned button_progress() override;
    std::string ota_state() override;

  private:
    DisplayRuntime &display_;
    DisplayTaskContext &context_;
    bool task_started_;
    DeviceSettingsService &settings_;
};
class TrendRestoreLoopAdapter final : public LoopTrendRestore {
  public:
    explicit TrendRestoreLoopAdapter(TrendRestore &restore) : restore_(restore) {}
    bool available() const override {
        return restore_.mutex != nullptr;
    }
    bool start() override;
    std::optional<TrendPoints> take() override;

  private:
    TrendRestore &restore_;
};
class OtaLoopAdapter final : public LoopOta {
  public:
    bool pending_verify() override;
    void roll_back() override;
    esp_err_t mark_valid() override;
};
} // namespace monitor

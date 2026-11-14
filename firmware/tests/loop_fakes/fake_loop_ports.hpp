#pragma once
// Recording fakes for the main loop's ports (God file refactor G14). Every call that changes
// something is appended to one shared trace, so a test can check the order of side effects.
#include "loop_ports.hpp"
#include <deque>
#include <string>
#include <vector>
namespace monitor {
using Trace = std::vector<std::string>;

class FakeLoopClock final : public LoopClock {
  public:
    int64_t now = 0;
    std::optional<int64_t> utc;
    int64_t now_ms() override {
        return now;
    }
    std::optional<int64_t> trusted_utc() override {
        return utc;
    }
    bool synchronized() override {
        return utc.has_value();
    }
    std::string utc_text() override {
        return utc ? "2027-01-15T08:00:00Z" : "";
    }
    std::string local_time_text() override {
        return utc ? "08:00" : "";
    }
};
class FakeLoopApi final : public LoopApi {
  public:
    explicit FakeLoopApi(Trace &trace) : trace_(trace) {}
    struct Update {
        uint64_t rom;
        Settings settings;
        bool retire;
    };
    std::deque<Update> updates;
    struct Hold {
        uint64_t rom;
        int64_t duration_ms;
        bool cancel;
    };
    std::deque<Hold> holds;
    std::optional<bool> writable;
    std::vector<Registry> published;
    bool next_sensor_update(uint64_t &rom, Settings &settings, bool &retire) override {
        if (updates.empty())
            return false;
        rom = updates.front().rom;
        settings = updates.front().settings;
        retire = updates.front().retire;
        updates.pop_front();
        trace_.push_back("api.take_update");
        return true;
    }
    bool next_calibration_hold(uint64_t &rom, int64_t &duration_ms, bool &cancel) override {
        if (holds.empty())
            return false;
        rom = holds.front().rom;
        duration_ms = holds.front().duration_ms;
        cancel = holds.front().cancel;
        holds.pop_front();
        trace_.push_back("api.take_hold");
        return true;
    }
    void set_sensor_settings_writable(bool value) override {
        writable = value;
        trace_.push_back(value ? "api.writable" : "api.read_only");
    }
    bool publish(const Registry &registry, ProbeScanStatus, int64_t, bool, bool,
                 const std::string &) override {
        published.push_back(registry);
        trace_.push_back("api.publish");
        return true;
    }

  private:
    Trace &trace_;
};
class FakeLoopSampling final : public LoopSampling {
  public:
    explicit FakeLoopSampling(Trace &trace) : trace_(trace) {}
    std::vector<Reading> readings;
    bool bus_fault = false;
    std::vector<uint64_t> written;
    std::vector<Reading> read(int64_t, const std::vector<uint64_t> &) override {
        trace_.push_back("sampling.read");
        return readings;
    }
    std::optional<ProbeMemory> store_probe_memory(uint64_t rom, ProbeMemory memory) override {
        written.push_back(rom);
        trace_.push_back("sampling.store_probe_memory");
        return memory;
    }
    bool fault() const override {
        return bus_fault;
    }
    bool enabled() const override {
        return true;
    }
    bool monitored() const override {
        return true;
    }
    ProbeScanStatus scan_status() const override {
        return {};
    }

  private:
    Trace &trace_;
};
class FakeLoopHistory final : public LoopHistory {
  public:
    explicit FakeLoopHistory(Trace &trace) : trace_(trace) {}
    std::vector<Bucket> buckets;
    std::vector<JournalEvent> events;
    bool submit(const Bucket &bucket) override {
        buckets.push_back(bucket);
        trace_.push_back("history.bucket");
        return true;
    }
    bool submit_event(const JournalEvent &event) override {
        events.push_back(event);
        trace_.push_back("history.event");
        return true;
    }
    bool healthy() const override {
        return true;
    }

  private:
    Trace &trace_;
};
class FakeLoopNetwork final : public LoopNetwork {
  public:
    LoopNetworkInfo network{"connected", false};
    LoopNetworkInfo info() const override {
        return network;
    }
    std::string mdns_hostname() const override {
        return "temperature-monitor";
    }
    std::string ap_ssid() const override {
        return "TempMonitor-SETUP";
    }
    std::string ap_password() const override {
        return "setup-key";
    }
};
class FakeLoopDisplay final : public LoopDisplay {
  public:
    explicit FakeLoopDisplay(Trace &trace) : trace_(trace) {}
    bool is_ready = true, accepts = true;
    std::vector<DisplayFrame> frames;
    bool ready() const override {
        return is_ready;
    }
    bool hand_over(DisplayFrame &&frame) override {
        trace_.push_back("display.hand_over");
        if (!accepts)
            return false;
        frames.push_back(std::move(frame));
        return true;
    }
    DeviceOptions options() override {
        return {};
    }
    unsigned button_progress() override {
        return 0;
    }
    std::string ota_state() override {
        return "valid";
    }

  private:
    Trace &trace_;
};
class FakeLoopTrendRestore final : public LoopTrendRestore {
  public:
    explicit FakeLoopTrendRestore(Trace &trace) : trace_(trace) {}
    bool is_available = true, starts = true;
    std::optional<TrendPoints> result;
    bool available() const override {
        return is_available;
    }
    bool start() override {
        trace_.push_back("trends.start");
        return starts;
    }
    std::optional<TrendPoints> take() override {
        auto taken = std::move(result);
        result.reset();
        return taken;
    }

  private:
    Trace &trace_;
};
class FakeLoopOta final : public LoopOta {
  public:
    explicit FakeLoopOta(Trace &trace) : trace_(trace) {}
    bool pending = false;
    esp_err_t valid_result = ESP_OK;
    bool pending_verify() override {
        return pending;
    }
    void roll_back() override {
        trace_.push_back("ota.roll_back");
    }
    esp_err_t mark_valid() override {
        trace_.push_back("ota.mark_valid");
        if (valid_result == ESP_OK)
            pending = false;
        return valid_result;
    }

  private:
    Trace &trace_;
};
// All loop fakes together, sharing one trace.
struct FakeLoopDevice {
    Trace trace;
    FakeLoopClock clock;
    FakeLoopApi api{trace};
    FakeLoopSampling sampling{trace};
    FakeLoopHistory history{trace};
    FakeLoopNetwork network;
    FakeLoopDisplay display{trace};
    FakeLoopTrendRestore trend_restore{trace};
    FakeLoopOta ota{trace};
    LoopPorts ports{clock, api, sampling, history, network, display, trend_restore, ota};
};
} // namespace monitor

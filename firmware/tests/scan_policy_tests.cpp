#include "scan_policy.hpp"
#include "sensor_registry.hpp"
#include <array>
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
namespace {
uint64_t rom(uint8_t serial) {
    std::array<uint8_t, 7> bytes{0x28, serial, 2, 3, 4, 5, 6};
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes.size(); ++i)
        value |= uint64_t(bytes[i]) << (8 * i);
    return value | (uint64_t(crc8(bytes.data(), bytes.size())) << 56);
}
ScanInput complete_scan(std::vector<uint64_t> roms) {
    ScanInput input;
    input.observed = std::move(roms);
    input.ended_normally = true;
    return input;
}
} // namespace
int main() {
    const std::vector<uint64_t> none;

    // The data line stuck low is a fault and stops the cycle.
    {
        ScanPolicy policy;
        CHECK(!policy.begin_cycle(true) && policy.fault());
        CHECK(policy.begin_cycle(false) && !policy.fault());
    }

    // A complete scan selects the probes to read, managed ones first, and rescans in 30 s.
    {
        ScanPolicy policy;
        CHECK(policy.begin_cycle(false) && policy.scan_due(0));
        CHECK(policy.apply_scan(complete_scan({rom(3), rom(1), rom(2)}), 0, {rom(2)}));
        CHECK(policy.has_probes() && policy.roms().size() == 3 && policy.roms()[0] == rom(2));
        CHECK(policy.status().complete && policy.status().discovered_count == 3);
        CHECK(!policy.fault());
        CHECK(!policy.scan_due(29999) && policy.scan_due(30000));
    }

    // An empty complete scan is accepted as "no probes": nothing to read, no fault.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({}), 0, none));
        CHECK(!policy.has_probes() && policy.status().complete && !policy.fault());
    }

    // A search that cannot start is a shared-bus fault and is retried in 5 s.
    {
        ScanPolicy policy;
        ScanInput failed;
        failed.iterator_created = false;
        CHECK(!policy.apply_scan(failed, 100, none));
        CHECK(policy.fault() && !policy.status().complete);
        CHECK(!policy.scan_due(4999 + 100) && policy.scan_due(5000 + 100));
    }

    // An incomplete, uncapped search (an error mid-way) is a fault with no reads, retried in 5 s.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1)}), 0, none));
        ScanInput broken;
        broken.observed = {rom(1)};
        CHECK(!policy.apply_scan(broken, 30000, none));
        CHECK(policy.fault() && !policy.status().complete);
        CHECK(policy.roms().size() == 1); // the previous read list is kept
        CHECK(policy.scan_due(35000));
    }

    // A capped search (more than 16 devices) keeps reading the known probes plus new ones.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1)}), 0, none));
        ScanInput capped;
        capped.capped = true;
        for (uint8_t serial = 2; serial < 20; ++serial)
            capped.observed.push_back(rom(serial));
        CHECK(policy.apply_scan(capped, 30000, {rom(1)}));
        CHECK(!policy.status().complete && policy.roms().size() == max_sensors);
        CHECK(policy.roms()[0] == rom(1) && !policy.fault());
        CHECK(policy.scan_due(35000));
    }

    // A failed bus reset, Convert T write or completion check is a fault and schedules a rescan.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2)}), 0, none));
        policy.transaction_failed(1000);
        CHECK(policy.fault() && policy.scan_due(6000) && !policy.scan_due(5999));
    }

    // After the reads: no probe answering is one shared-bus fault; a single failed probe is not.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2)}), 0, none));
        policy.finish_reads(1000, 0, true);
        CHECK(policy.fault() && policy.scan_due(6000));
        policy.finish_reads(2000, 1, true);
        CHECK(!policy.fault() && policy.scan_due(7000)); // missing value: rescan soon
        policy.finish_reads(3000, 2, false);
        CHECK(!policy.fault());
    }

    // A scan that finds fewer probes than are being read is suspect (the driver reports the
    // same "no more devices" for a glitch mid-search): keep reading the old list, report the
    // scan as incomplete so nothing can be retired on it, and rescan within 5 s.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(3)}), 0, none));
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(3)}), 30000, none));
        CHECK(policy.roms().size() == 3);
        CHECK(!policy.status().complete && !policy.fault());
        CHECK(!policy.scan_due(34999) && policy.scan_due(35000));
        // A second scan that agrees accepts the smaller set.
        CHECK(policy.apply_scan(complete_scan({rom(3), rom(1)}), 35000, none));
        CHECK(policy.roms().size() == 2 && policy.status().complete);
        CHECK(policy.status().discovered_count == 2);
        CHECK(!policy.scan_due(64999) && policy.scan_due(65000));
    }

    // If the second scan finds everything again, the glitch is forgotten and the next smaller
    // scan has to be confirmed all over again.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(3)}), 0, none));
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(3)}), 30000, none));
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(3)}), 35000, none));
        CHECK(policy.roms().size() == 3 && policy.status().complete);
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(3)}), 65000, none));
        CHECK(policy.roms().size() == 3 && !policy.status().complete);
    }

    // A second scan that is smaller in a different way is still suspect.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(3)}), 0, none));
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(3)}), 30000, none));
        CHECK(policy.apply_scan(complete_scan({rom(1)}), 35000, none));
        CHECK(policy.roms().size() == 3 && !policy.status().complete);
        CHECK(policy.apply_scan(complete_scan({rom(1)}), 40000, none));
        CHECK(policy.roms().size() == 1 && policy.status().complete);
    }

    // More probes than before are accepted immediately; a replacement (one gone, one new) is
    // suspect until confirmed.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2)}), 0, none));
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(4)}), 30000, none));
        CHECK(policy.roms().size() == 3 && policy.status().complete);
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(5)}), 60000, none));
        CHECK(policy.roms().size() == 3 && !policy.status().complete);
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(5)}), 65000, none));
        CHECK(policy.status().complete && policy.roms().size() == 3);
        bool has_five = false;
        for (auto id : policy.roms())
            has_five = has_five || id == rom(5);
        CHECK(has_five);
    }

    // No device answers at all while several probes are being read: one shared-bus fault, the old
    // read list is kept, and it is rechecked every 5 s (no flicker between rescans).
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(3)}), 0, none));
        CHECK(!policy.apply_scan(complete_scan({}), 30000, none));
        CHECK(policy.fault() && policy.roms().size() == 3 && !policy.status().complete);
        CHECK(!policy.scan_due(34999) && policy.scan_due(35000));
        CHECK(policy.begin_cycle(false) && policy.fault()); // still faulty between rescans
        // The probes come back: the fault clears and everything is read again.
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2), rom(3)}), 35000, none));
        CHECK(!policy.fault() && policy.status().complete && policy.roms().size() == 3);
        CHECK(policy.begin_cycle(false) && !policy.fault());
    }

    // Empty scans that never end are believed after five minutes: the probes really are gone.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2)}), 0, none));
        int64_t now = 30000;
        for (; now < 30000 + 300000; now += 5000) {
            CHECK(!policy.apply_scan(complete_scan({}), now, none));
            CHECK(policy.fault() && policy.roms().size() == 2);
        }
        CHECK(policy.apply_scan(complete_scan({}), now, none));
        CHECK(!policy.fault() && !policy.has_probes() && policy.status().complete);
        CHECK(policy.begin_cycle(false) && !policy.fault());
    }

    // A single known probe that disappears is just a missing probe: confirmed by a second scan,
    // never a bus fault.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1)}), 0, none));
        CHECK(policy.apply_scan(complete_scan({}), 30000, none));
        CHECK(!policy.fault() && policy.has_probes() && !policy.status().complete);
        CHECK(policy.apply_scan(complete_scan({}), 35000, none));
        CHECK(!policy.fault() && !policy.has_probes() && policy.status().complete);
    }

    // Reads that work again also end a no-presence fault.
    {
        ScanPolicy policy;
        CHECK(policy.apply_scan(complete_scan({rom(1), rom(2)}), 0, none));
        CHECK(!policy.apply_scan(complete_scan({}), 30000, none));
        CHECK(policy.fault());
        policy.finish_reads(31000, 2, false);
        CHECK(!policy.fault());
        CHECK(policy.begin_cycle(false) && !policy.fault());
    }

    // End to end with the registry: all probes vanishing is one bus-level event and no sensor
    // ever reaches `disconnected`.
    {
        Registry registry;
        ScanPolicy policy;
        const std::vector<uint64_t> ids{rom(1), rom(2), rom(3)};
        std::vector<Reading> readings;
        for (auto id : ids)
            readings.push_back({id, 20});
        registry.sample(readings, 0);
        CHECK(policy.apply_scan(complete_scan(ids), 0, none));
        int64_t now = 5000;
        for (int cycle = 0; cycle < 6; ++cycle, now += 5000) {
            CHECK(policy.begin_cycle(false));
            if (policy.scan_due(now))
                CHECK(!policy.apply_scan(complete_scan({}), now, none));
            else if (policy.has_probes())
                policy.transaction_failed(now); // the bus reset finds nobody on the wire
            registry.sample({}, now, policy.fault());
            for (const auto &sensor : registry.sensors())
                CHECK(sensor.state == State::bus_fault);
        }
        size_t bus_events = 0, sensor_events = 0;
        for (const auto &event : registry.events())
            (event.rom == 0 ? bus_events : sensor_events)++;
        CHECK(bus_events == 1 && sensor_events == 0);
    }
    std::cout << "PASS: scan policy reproduces the 1-Wire service's scan and fault decisions\n";
}

#include "event_store.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <unistd.h>

using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int main() {
    char root[] = "/tmp/monitor-event-cut-XXXXXX";
    CHECK(mkdtemp(root));
    const auto path = std::filesystem::path(root) / "events.bin";

    JournalEvent first{};
    first.boot_id = 1;
    first.uptime_ms = 1000;
    first.kind = EventKind::reboot;
    JournalEvent next{};
    next.boot_id = 1;
    next.uptime_ms = 2000;
    next.kind = EventKind::wifi_state;
    next.state = State::high;

    auto first_encoded = first;
    first_encoded.sequence = 1;
    auto next_encoded = next;
    next_encoded.sequence = 2;
    const auto committed = encode_event(first_encoded);
    const auto interrupted = encode_event(next_encoded);
    CHECK(committed.size() == event_record_bytes && interrupted.size() == event_record_bytes);

    // Simulate power removal after each possible byte of the next append.
    // Reopening must retain the complete event, discard only the incomplete
    // tail, and allow a fresh append with the next sequence number.
    for (size_t cut = 0; cut < interrupted.size(); ++cut) {
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            CHECK(output.good());
            output.write(reinterpret_cast<const char *>(committed.data()), committed.size());
            output.write(reinterpret_cast<const char *>(interrupted.data()), cut);
            CHECK(output.good());
        }
        EventStore recovered(path.string());
        CHECK(recovered.recover());
        CHECK(recovered.bytes() == committed.size());
        auto page = recovered.page(0, 50);
        CHECK(page.available && page.events.size() == 1);
        CHECK(page.events.front().kind == EventKind::reboot);
        CHECK(page.next_after == 1);
        CHECK(recovered.append(next));
        page = recovered.page(0, 50);
        CHECK(page.available && page.events.size() == 2);
        CHECK(page.events.back().kind == EventKind::wifi_state);
        CHECK(page.next_after == 2);
    }

    // Future formats must be preserved even if power fails before the sixth
    // header byte. An old firmware image must not silently erase them.
    const std::array<std::array<uint8_t, 6>, 3> future_headers{{
        {{'T', 'M', 'E', '2', 2, 0}},
        {{'T', 'M', 'E', '1', 2, 0}},
        {{'T', 'M', 'E', '1', 1, 1}},
    }};
    for (const auto &future : future_headers) {
        for (size_t count = 4; count <= future.size(); ++count) {
            // A different minor version only becomes identifiable at byte 6.
            if (future[3] == '1' && future[4] == 1 && count < 6)
                continue;
            {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                CHECK(output.good());
                output.write(reinterpret_cast<const char *>(committed.data()), committed.size());
                output.write(reinterpret_cast<const char *>(future.data()), count);
                CHECK(output.good());
            }
            const auto expected_size = committed.size() + count;
            EventStore recovered(path.string());
            CHECK(!recovered.recover());
            CHECK(std::filesystem::file_size(path) == expected_size);
        }
    }

    std::filesystem::remove_all(root);
    std::cout << "PASS: event journal interrupted append prefixes and future headers preserved\n";
}

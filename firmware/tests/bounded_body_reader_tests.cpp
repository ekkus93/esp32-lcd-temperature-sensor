#include "bounded_body_reader.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
using namespace monitor;
namespace {
struct FakeReceive {
    std::string bytes;
    size_t offset = 0;
    size_t max_chunk = SIZE_MAX;
    int64_t now = 0;
    int64_t delay = 0;
    bool fail = false;
    unsigned calls = 0;
};
int receive(void *opaque, char *buffer, size_t size) {
    auto &fake = *static_cast<FakeReceive *>(opaque);
    ++fake.calls;
    if (fake.fail)
        return -1;
    if (fake.offset == fake.bytes.size())
        return 0;
    const auto count = std::min({size, fake.max_chunk, fake.bytes.size() - fake.offset});
    std::copy_n(fake.bytes.data() + fake.offset, count, buffer);
    fake.offset += count;
    fake.now += fake.delay;
    return static_cast<int>(count);
}
int64_t clock_ms(void *opaque) {
    return static_cast<FakeReceive *>(opaque)->now;
}
} // namespace
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    std::string body;
    FakeReceive normal{"{\"x\":1}"};
    CHECK(read_exact_body(normal.bytes.size(), 2048, 5000, &normal, receive, clock_ms, body) ==
          BodyReadStatus::complete);
    CHECK(body == normal.bytes);

    FakeReceive slow{"1234567", 0, 1, 0, 2000};
    CHECK(read_exact_body(slow.bytes.size(), 2048, 5000, &slow, receive, clock_ms, body) ==
          BodyReadStatus::timeout);
    CHECK(body.empty() && slow.calls == 3 && slow.offset == 3);

    FakeReceive exact_deadline{"x", 0, SIZE_MAX, 0, 5000};
    CHECK(read_exact_body(1, 2048, 5000, &exact_deadline, receive, clock_ms, body) ==
          BodyReadStatus::timeout);
    CHECK(body.empty());

    FakeReceive eof{"ab"};
    CHECK(read_exact_body(3, 2048, 5000, &eof, receive, clock_ms, body) == BodyReadStatus::eof);
    CHECK(body.empty());

    FakeReceive pipelined{"{}GET /next"};
    CHECK(read_exact_body(2, 2048, 5000, &pipelined, receive, clock_ms, body) ==
          BodyReadStatus::complete);
    CHECK(body == "{}" && pipelined.offset == 2);

    FakeReceive oversized{"data"};
    CHECK(read_exact_body(4, 3, 5000, &oversized, receive, clock_ms, body) ==
          BodyReadStatus::too_large);
    CHECK(oversized.calls == 0 && body.empty());

    FakeReceive failed{"data"};
    failed.fail = true;
    CHECK(read_exact_body(4, 2048, 5000, &failed, receive, clock_ms, body) ==
          BodyReadStatus::receive_error);
    CHECK(body.empty());

    FakeReceive discard{"untrusted request body"};
    CHECK(discard_exact_body(discard.bytes.size(), 5000, &discard, receive, clock_ms) ==
          BodyReadStatus::complete);
    CHECK(discard.offset == discard.bytes.size());
    FakeReceive slow_discard{std::string(1024, 'x'), 0, 128, 0, 2000};
    CHECK(discard_exact_body(slow_discard.bytes.size(), 5000, &slow_discard, receive, clock_ms) ==
          BodyReadStatus::timeout);
    CHECK(slow_discard.offset == 384);
    std::cout
        << "PASS: exact body framing, pipeline boundary, EOF, oversized input and total deadline\n";
}

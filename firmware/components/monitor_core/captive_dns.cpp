#include "captive_dns.hpp"
#include <cstddef>
namespace monitor {
std::optional<std::vector<uint8_t>> captive_dns_reply(const std::vector<uint8_t> &q) {
    if (q.size() < 17 || q.size() > 512)
        return std::nullopt;
    auto word = [&](size_t at) { return (unsigned(q[at]) << 8) | q[at + 1]; };
    if ((word(2) & 0xfa00) || word(4) != 1 || word(6) || word(8) || word(10) > 1)
        return std::nullopt;
    size_t cursor = 12;
    while (cursor < q.size() && q[cursor]) {
        unsigned length = q[cursor++];
        if (length > 63 || cursor + length >= q.size() || cursor + length - 12 > 254)
            return std::nullopt;
        cursor += length;
    }
    if (cursor >= q.size() || ++cursor + 4 > q.size())
        return std::nullopt;
    const auto type = word(cursor), klass = word(cursor + 2);
    cursor += 4;
    const auto question_end = cursor;
    if (klass != 1)
        return std::nullopt;
    if (word(10)) {
        // Optional EDNS0 OPT; discard extensions rather than amplify the reply.
        if (cursor + 11 > q.size() || q[cursor] || word(cursor + 1) != 41 || q[cursor + 6] ||
            cursor + 11 + word(cursor + 9) != q.size())
            return std::nullopt;
        cursor = q.size();
    }
    if (cursor != q.size())
        return std::nullopt;
    std::vector<uint8_t> answer(q.begin(), q.begin() + question_end);
    answer[2] = 0x84 | (q[2] & 1); // QR, authoritative, preserve RD; never claim recursion/DNSSEC
    answer[3] = 0;
    answer[6] = 0;
    answer[7] = type == 1 ? 1 : 0;
    answer[8] = answer[9] = answer[10] = answer[11] = 0;
    if (type == 1) {
        const uint8_t record[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1};
        answer.insert(answer.end(), record, record + sizeof(record));
    }
    return answer;
}
} // namespace monitor

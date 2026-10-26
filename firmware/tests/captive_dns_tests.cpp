#include "captive_dns.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
using namespace monitor;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __LINE__ << ": " #expr << '\n';                                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    const std::vector<uint8_t> query = {0x12, 0x34, 1,   0,   0,   1,   0,   0,   0,   0, 0,
                                        0,    7,    'e', 'x', 'a', 'm', 'p', 'l', 'e', 7, 'i',
                                        'n',  'v',  'a', 'l', 'i', 'd', 0,   0,   1,   0, 1};
    auto answer = captive_dns_reply(query);
    CHECK(answer && answer->size() == query.size() + 16 && (*answer)[7] == 1);
    CHECK((*answer)[0] == 0x12 && (*answer)[1] == 0x34 && (*answer)[answer->size() - 4] == 192);
    for (size_t i = 0; i < query.size(); ++i)
        CHECK(!captive_dns_reply(std::vector<uint8_t>(query.begin(), query.begin() + i)));
    auto aaaa = query;
    aaaa[aaaa.size() - 3] = 28;
    CHECK(captive_dns_reply(aaaa)->size() == query.size() && (*captive_dns_reply(aaaa))[7] == 0);
    auto edns = query;
    edns[11] = 1;
    const uint8_t opt[] = {0, 0, 41, 2, 0, 0, 0, 0, 0, 0, 0};
    edns.insert(edns.end(), opt, opt + sizeof(opt));
    CHECK(captive_dns_reply(edns)->size() == answer->size());
    auto compressed = query;
    compressed[12] = 0xc0;
    compressed[13] = 12;
    CHECK(!captive_dns_reply(compressed));
    auto response = query;
    response[2] |= 0x80;
    CHECK(!captive_dns_reply(response));
    auto many = query;
    many[5] = 2;
    CHECK(!captive_dns_reply(many));
    auto trailing = query;
    trailing.push_back(0);
    CHECK(!captive_dns_reply(trailing));
    std::mt19937 random(0x5344);
    for (unsigned i = 0; i < 10000; ++i) {
        std::vector<uint8_t> fuzz;
        if (i % 2) {
            fuzz = query;
            fuzz[random() % fuzz.size()] ^= uint8_t(random());
        } else {
            fuzz.resize(random() % 600);
            for (auto &byte : fuzz)
                byte = random();
        }
        auto result = captive_dns_reply(fuzz);
        CHECK(!result || result->size() <= 528);
    }
    std::cout << "PASS: captive DNS A/AAAA, EDNS, truncation, compressed loops and 10000 malformed "
                 "packets\n";
}

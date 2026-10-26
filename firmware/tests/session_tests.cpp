#include "sessions.hpp"
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
    SessionStore store;
    auto token = std::string(64, 'a');
    auto csrf = std::string(48, 'b');
    CHECK(!store.authorize(token, 0));
    CHECK(!store.create("", csrf, 0));
    CHECK(store.create(token, csrf, 1000));
    CHECK(store.authorize(token, 1000));
    CHECK(store.authorize(token, 3600999));
    CHECK(!store.authorize(token, 3601000));
    CHECK(!store.authorize(std::string(64, 'c'), 1000));
    CHECK(store.create(token, csrf, 4000000));
    auto *session = store.authorize(token, 4000001);
    CHECK(session);
    store.logout(session->token);
    CHECK(!store.authorize(token, 4000002));
    for (unsigned i = 0; i < 4; ++i)
        CHECK(store.create(std::string(64, char('a' + i)), csrf, 5000000 + i));
    CHECK(store.create(std::string(64, 'e'), csrf, 5000010));
    CHECK(!store.authorize(token, 5000011)); // Oldest session evicted, capacity remains four.
    CHECK(store.authorize(std::string(64, 'e'), 5000011));
    store.clear();
    CHECK(!store.authorize(std::string(64, 'e'), 5000012));
    CHECK(constant_equal("fixed-value", "fixed-value"));
    CHECK(!constant_equal("fixed-value", "fixed-other"));
    std::cout << "PASS: session expiry, forged tokens, logout, reset and bounded capacity\n";
}

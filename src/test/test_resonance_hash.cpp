#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_constants.h"
#include <cstdint>
#include <cstring>

using namespace resonance;

TEST_CASE("fnv1a_hash empty returns offset basis", "[fnv1a_hash]") {
    uint64_t h = fnv1a_hash(nullptr, 0);
    REQUIRE(h == kFNVOffsetBasis);
}

TEST_CASE("fnv1a_hash single byte", "[fnv1a_hash]") {
    uint8_t data[] = {0x61};
    uint64_t h = fnv1a_hash(data, 1);
    REQUIRE(h != kFNVOffsetBasis);
    REQUIRE(h != 0);
}

TEST_CASE("fnv1a_hash deterministic", "[fnv1a_hash]") {
    uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    uint64_t h1 = fnv1a_hash(data, 5);
    uint64_t h2 = fnv1a_hash(data, 5);
    REQUIRE(h1 == h2);
}

TEST_CASE("fnv1a_hash different data different hash", "[fnv1a_hash]") {
    uint8_t data1[] = {0x61, 0x62, 0x63};
    uint8_t data2[] = {0x61, 0x62, 0x64};
    uint64_t h1 = fnv1a_hash(data1, 3);
    uint64_t h2 = fnv1a_hash(data2, 3);
    REQUIRE(h1 != h2);
}

TEST_CASE("fnv1a_hash length affects result", "[fnv1a_hash]") {
    uint8_t data[] = {0x61, 0x62};
    uint64_t h1 = fnv1a_hash(data, 1);
    uint64_t h2 = fnv1a_hash(data, 2);
    REQUIRE(h1 != h2);
}

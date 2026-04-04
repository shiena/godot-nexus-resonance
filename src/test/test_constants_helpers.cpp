#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_constants.h"
#include <cmath>

using namespace resonance;

TEST_CASE("is_valid_ambisonic_channel_count accepts first three orders", "[constants]") {
    REQUIRE(is_valid_ambisonic_channel_count(4));
    REQUIRE(is_valid_ambisonic_channel_count(9));
    REQUIRE(is_valid_ambisonic_channel_count(16));
}

TEST_CASE("is_valid_ambisonic_channel_count rejects other counts", "[constants]") {
    REQUIRE_FALSE(is_valid_ambisonic_channel_count(0));
    REQUIRE_FALSE(is_valid_ambisonic_channel_count(1));
    REQUIRE_FALSE(is_valid_ambisonic_channel_count(3));
    REQUIRE_FALSE(is_valid_ambisonic_channel_count(8));
    REQUIRE_FALSE(is_valid_ambisonic_channel_count(17));
}

TEST_CASE("ambisonic and path EQ constants match documented values", "[constants]") {
    REQUIRE(kAmbisonicWChannelScale == Approx(1.0f / std::sqrt(2.0f)));
    REQUIRE(kPathEQCoeffMin == Approx(1e-6f));
    REQUIRE(kPathEQCoeffMax == Approx(1.0f));
}

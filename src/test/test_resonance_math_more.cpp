#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_math.h"
#include <cmath>
#include <limits>

using namespace resonance;

TEST_CASE("apply_volume_ramp nullptr buffer is no-op", "[resonance_math]") {
    apply_volume_ramp(0.0f, 1.0f, 8, nullptr);
    SUCCEED();
}

TEST_CASE("reverb_ir_size_samples zero duration", "[resonance_math]") {
    REQUIRE(reverb_ir_size_samples(48000, 0.0f) == 0);
}

TEST_CASE("reverb_ir_size_samples non-finite duration sanitized", "[resonance_math]") {
    REQUIRE(reverb_ir_size_samples(48000, std::numeric_limits<float>::quiet_NaN()) == 0);
    REQUIRE(reverb_ir_size_samples(48000, std::numeric_limits<float>::infinity()) == 0);
}

TEST_CASE("sanitize_delay_samples identity and rounding path", "[resonance_math]") {
    REQUIRE(sanitize_delay_samples(0) == 0);
    REQUIRE(sanitize_delay_samples(100) == 100);
    REQUIRE(sanitize_delay_samples(-42) == -42);
    REQUIRE(sanitize_delay_samples(3) == 3);
}

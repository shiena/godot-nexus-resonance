#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_math.h"
#include <cmath>
#include <limits>

using namespace resonance;

TEST_CASE("apply_volume_ramp constant volume", "[volume_ramp]") {
    float buffer[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    apply_volume_ramp(0.5f, 0.5f, 4, buffer);
    REQUIRE(buffer[0] == Approx(0.5f));
    REQUIRE(buffer[1] == Approx(0.5f));
    REQUIRE(buffer[2] == Approx(0.5f));
    REQUIRE(buffer[3] == Approx(0.5f));
}

TEST_CASE("apply_volume_ramp constant unity no change", "[volume_ramp]") {
    float buffer[4] = {2.0f, 3.0f, 4.0f, 5.0f};
    apply_volume_ramp(1.0f, 1.0f, 4, buffer);
    REQUIRE(buffer[0] == Approx(2.0f));
    REQUIRE(buffer[1] == Approx(3.0f));
    REQUIRE(buffer[2] == Approx(4.0f));
    REQUIRE(buffer[3] == Approx(5.0f));
}

TEST_CASE("apply_volume_ramp 0 to 1 over N samples", "[volume_ramp]") {
    float buffer[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    apply_volume_ramp(0.0f, 1.0f, 4, buffer);
    // Sample 0: vol 0.0, 1*0=0
    // Sample 1: vol 0.25, 1*0.25=0.25
    // Sample 2: vol 0.5, 1*0.5=0.5
    // Sample 3: vol 0.75, 1*0.75=0.75
    REQUIRE(buffer[0] == Approx(0.0f));
    REQUIRE(buffer[1] == Approx(0.25f));
    REQUIRE(buffer[2] == Approx(0.5f));
    REQUIRE(buffer[3] == Approx(0.75f));
}

TEST_CASE("apply_volume_ramp num_samples zero is no-op", "[volume_ramp]") {
    float buffer[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    apply_volume_ramp(0.0f, 1.0f, 0, buffer);
    REQUIRE(buffer[0] == 1.0f);
    REQUIRE(buffer[1] == 2.0f);
    REQUIRE(buffer[2] == 3.0f);
    REQUIRE(buffer[3] == 4.0f);
}

TEST_CASE("apply_volume_ramp 1 to 0 ramp down", "[volume_ramp]") {
    float buffer[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    apply_volume_ramp(1.0f, 0.0f, 4, buffer);
    REQUIRE(buffer[0] == Approx(1.0f));
    REQUIRE(buffer[1] == Approx(0.75f));
    REQUIRE(buffer[2] == Approx(0.5f));
    REQUIRE(buffer[3] == Approx(0.25f));
}

TEST_CASE("sanitize_audio_float finite unchanged", "[resonance_math]") {
    REQUIRE(sanitize_audio_float(1.0f) == 1.0f);
    REQUIRE(sanitize_audio_float(-0.5f) == -0.5f);
    REQUIRE(sanitize_audio_float(0.0f) == 0.0f);
}

TEST_CASE("sanitize_audio_float nan becomes zero", "[resonance_math]") {
    float nan_val = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(sanitize_audio_float(nan_val) == 0.0f);
}

TEST_CASE("sanitize_audio_float inf becomes zero", "[resonance_math]") {
    REQUIRE(sanitize_audio_float(std::numeric_limits<float>::infinity()) == 0.0f);
    REQUIRE(sanitize_audio_float(-std::numeric_limits<float>::infinity()) == 0.0f);
}

TEST_CASE("clamp_reverb_time valid above 0.1", "[resonance_math]") {
    REQUIRE(clamp_reverb_time(0.5f) == Approx(0.5f));
    REQUIRE(clamp_reverb_time(2.0f) == Approx(2.0f));
}

TEST_CASE("clamp_reverb_time below 0.1 clamped", "[resonance_math]") {
    REQUIRE(clamp_reverb_time(0.05f) == Approx(0.1f));
    REQUIRE(clamp_reverb_time(0.0f) == Approx(0.1f));
}

TEST_CASE("sanitize_delay_samples finite unchanged", "[resonance_math]") {
    REQUIRE(sanitize_delay_samples(0) == 0);
    REQUIRE(sanitize_delay_samples(42) == 42);
}

TEST_CASE("reverb_ir_size_samples nominal", "[resonance_math]") {
    REQUIRE(reverb_ir_size_samples(48000, 2.0f) == 96000);
    REQUIRE(reverb_ir_size_samples(44100, 1.0f) == 44100);
}

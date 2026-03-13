#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_math.h"
#include <cmath>

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

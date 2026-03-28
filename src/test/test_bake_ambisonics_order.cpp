#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_constants.h"

TEST_CASE("clamp_bake_ambisonics_order clamps to 1-3") {
    REQUIRE(resonance::clamp_bake_ambisonics_order(0) == 1);
    REQUIRE(resonance::clamp_bake_ambisonics_order(1) == 1);
    REQUIRE(resonance::clamp_bake_ambisonics_order(2) == 2);
    REQUIRE(resonance::clamp_bake_ambisonics_order(3) == 3);
    REQUIRE(resonance::clamp_bake_ambisonics_order(99) == 3);
    REQUIRE(resonance::clamp_bake_ambisonics_order(-100) == 1);
}

#include "../lib/catch2/single_include/catch2/catch.hpp"
#include "../resonance_constants.h"

TEST_CASE("clamp_physics_ray_batch_size bounds", "[physics_ray_batch][config]") {
    REQUIRE(resonance::clamp_physics_ray_batch_size(0) == resonance::kPhysicsRayBatchSizeMin);
    REQUIRE(resonance::clamp_physics_ray_batch_size(1) == 1);
    REQUIRE(resonance::clamp_physics_ray_batch_size(16) == 16);
    REQUIRE(resonance::clamp_physics_ray_batch_size(256) == 256);
    REQUIRE(resonance::clamp_physics_ray_batch_size(300) == resonance::kPhysicsRayBatchSizeMax);
}

TEST_CASE("default physics ray batch constant", "[physics_ray_batch][config]") {
    REQUIRE(resonance::kDefaultPhysicsRayBatchSize == 16);
}

TEST_CASE("Custom scene_type 3 with batch maps to batched Phonon path semantics", "[physics_ray_batch][config]") {
    auto uses_batched_custom_jobs = [](int config_scene_type, int clamped_batch) { return config_scene_type == 3 && clamped_batch > 1; };
    REQUIRE_FALSE(uses_batched_custom_jobs(0, resonance::clamp_physics_ray_batch_size(16)));
    REQUIRE_FALSE(uses_batched_custom_jobs(1, resonance::clamp_physics_ray_batch_size(16)));
    REQUIRE(uses_batched_custom_jobs(3, resonance::clamp_physics_ray_batch_size(16)));
    REQUIRE_FALSE(uses_batched_custom_jobs(3, resonance::clamp_physics_ray_batch_size(1)));
}

extends GutTest

## Unit tests for [ResonanceRuntimePerfMonitors] static helpers.


func test_simulation_worker_timing_sum_empty_dict() -> void:
	assert_eq(ResonanceRuntimePerfMonitors.simulation_worker_timing_sum({}), 0)


func test_simulation_worker_timing_sum_partial_keys() -> void:
	assert_eq(
		ResonanceRuntimePerfMonitors.simulation_worker_timing_sum({"us_run_direct": 5}),
		5
	)


func test_simulation_worker_timing_sum_all_keys() -> void:
	var w: Dictionary = {
		"us_run_direct": 1,
		"us_run_reflections": 2,
		"us_run_pathing": 3,
		"us_sync_fetch": 4,
	}
	assert_eq(ResonanceRuntimePerfMonitors.simulation_worker_timing_sum(w), 10)

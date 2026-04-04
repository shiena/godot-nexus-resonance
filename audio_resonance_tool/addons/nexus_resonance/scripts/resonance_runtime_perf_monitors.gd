extends Object
class_name ResonanceRuntimePerfMonitors

## Registers Debugger custom monitors for [ResonanceRuntime]. Reading callables stay on the runtime [Node].

const MON_MAIN := "Nexus Resonance/Main/runtime_last_tick_us"
const MON_PHYS := "Nexus Resonance/Main/runtime_physics_tick_us"
const MON_W_DIRECT := "Nexus Resonance/Worker/us_run_direct"
const MON_W_REFL := "Nexus Resonance/Worker/us_run_reflections"
const MON_W_PATH := "Nexus Resonance/Worker/us_run_pathing"
const MON_W_SYNC := "Nexus Resonance/Worker/us_sync_fetch"
const MON_W_SUM := "Nexus Resonance/Worker/last_tick_sum_us"


static func monitor_ids() -> Array[String]:
	return [
		MON_MAIN,
		MON_PHYS,
		MON_W_DIRECT,
		MON_W_REFL,
		MON_W_PATH,
		MON_W_SYNC,
		MON_W_SUM,
	]


## Sum of worker timing fields from [method ResonanceServer.get_simulation_worker_timing] (microseconds).
static func simulation_worker_timing_sum(w: Dictionary) -> int:
	return (
		int(w.get("us_run_direct", 0))
		+ int(w.get("us_run_reflections", 0))
		+ int(w.get("us_run_pathing", 0))
		+ int(w.get("us_sync_fetch", 0))
	)


static func register(owner: Node) -> void:
	var existing: Array = Performance.get_custom_monitor_names()
	for id in monitor_ids():
		if id in existing:
			Performance.remove_custom_monitor(id)
	Performance.add_custom_monitor(MON_MAIN, Callable(owner, "_nexus_perf_read_main_usec"))
	Performance.add_custom_monitor(MON_PHYS, Callable(owner, "_nexus_perf_read_physics_tick_usec"))
	Performance.add_custom_monitor(
		MON_W_DIRECT, Callable(owner, "_nexus_perf_worker_field").bind("us_run_direct")
	)
	Performance.add_custom_monitor(
		MON_W_REFL, Callable(owner, "_nexus_perf_worker_field").bind("us_run_reflections")
	)
	Performance.add_custom_monitor(
		MON_W_PATH, Callable(owner, "_nexus_perf_worker_field").bind("us_run_pathing")
	)
	Performance.add_custom_monitor(
		MON_W_SYNC, Callable(owner, "_nexus_perf_worker_field").bind("us_sync_fetch")
	)
	Performance.add_custom_monitor(MON_W_SUM, Callable(owner, "_nexus_perf_read_worker_sum"))


static func unregister_all() -> void:
	if Engine.is_editor_hint():
		return
	for id in monitor_ids():
		Performance.remove_custom_monitor(id)

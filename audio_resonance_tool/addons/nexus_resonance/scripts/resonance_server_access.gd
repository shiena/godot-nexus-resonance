extends Object
class_name ResonanceServerAccess

## Central access to the [ResonanceServer] engine singleton (GDExtension).
## GDScript has no static type for that class; [method get_server] and [method get_server_if_initialized]
## return [Variant] ([code]null[/code] or the native instance).

## Engine singleton name for the Nexus Resonance native API.
const SINGLETON_NAME := &"ResonanceServer"


## Returns [code]true[/code] if the singleton is registered (extension loaded).
static func has_server() -> bool:
	return Engine.has_singleton(SINGLETON_NAME)


## Returns the native server instance, or [code]null[/code] if the singleton is missing.
static func get_server() -> Variant:
	if not has_server():
		return null
	return Engine.get_singleton(SINGLETON_NAME)


## Returns the server if it exists and reports initialized, else [code]null[/code].
static func get_server_if_initialized() -> Variant:
	var s: Variant = get_server()
	if s == null:
		return null
	if not s.has_method("is_initialized") or not s.is_initialized():
		return null
	return s

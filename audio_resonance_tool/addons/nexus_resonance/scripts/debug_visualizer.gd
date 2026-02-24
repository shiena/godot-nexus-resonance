extends Node3D

## Debug Visualizer (Physics Fallback)
##
## NOTE: This visualizer uses Godot's Physics Raycasts (DirectSpaceState).
## It does NOT visualize the actual Steam Audio Embree geometry.
## If your visual geometry differs from your collision geometry, this debug view might differ from the audio result.
##
## For accurate Audio Debugging, enable 'Debug Occlusion' in the ResonanceRuntime node,
## which draws lines based on the actual C++ Raycast data.

## Camera used as listener position for the debug ray. Usually the active 3D camera. Green line = clear path, red = blocked (uses Godot Physics, not Steam Audio geometry).
@export var listener_camera: Camera3D
## 3D node representing the sound source. Used as ray endpoint for occlusion visualization. Draws a line from listener to source; color indicates if path is blocked by Physics collision.
@export var audio_source_node: Node3D 

var debug_mesh_instance: MeshInstance3D
var immediate_mesh: ImmediateMesh

func _ready():
	debug_mesh_instance = MeshInstance3D.new()
	immediate_mesh = ImmediateMesh.new()
	debug_mesh_instance.mesh = immediate_mesh
	debug_mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	
	var mat = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.no_depth_test = true 
	debug_mesh_instance.material_override = mat
	
	add_child(debug_mesh_instance)

func _process(_delta):
	if not listener_camera or not audio_source_node:
		return
		
	var start = listener_camera.global_position
	var end = audio_source_node.global_position
	
	# REVIEW FIX: ImmediateMesh is slow. 
	# However, for debugging purposes, recreating it per frame is acceptable if geometry counts are low (1 line).
	# We stick to this for simplicity but acknowledge it's not for production.
	
	immediate_mesh.clear_surfaces()
	immediate_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	
	var color = Color.GREEN # Green = Clear
	
	var space_state = get_world_3d().direct_space_state
	# Ensure we have a space state (can be null during scene loading)
	if space_state:
		var query = PhysicsRayQueryParameters3D.create(start, end)
		var result = space_state.intersect_ray(query)
		
		if result:
			color = Color.RED # Red = Blocked (by Godot Physics)
	
	immediate_mesh.surface_set_color(color)
	immediate_mesh.surface_add_vertex(start - global_position) 
	immediate_mesh.surface_add_vertex(end - global_position)
	immediate_mesh.surface_end()

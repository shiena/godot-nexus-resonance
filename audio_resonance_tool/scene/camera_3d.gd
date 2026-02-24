extends Camera3D

# Settings
@export var move_speed: float = 5.0
@export var fast_move_speed: float = 15.0
@export var look_sensitivity: float = 0.002

# Internal variables
#var _velocity = Vector3.ZERO
var _yaw: float = 0.0
var _pitch: float = 0.0

func _ready() -> void:
	# Capture the mouse immediately
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	
	# Take initial values from current rotation so camera does not jump
	_yaw = rotation.y
	_pitch = rotation.x

func _input(event: InputEvent) -> void:
	# Mouse Look Logic
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		# Accumulate values in variables instead of rotating directly
		_yaw -= event.relative.x * look_sensitivity
		_pitch -= event.relative.y * look_sensitivity
		
		# Clamp Pitch (prevent flipping over)
		_pitch = clamp(_pitch, deg_to_rad(-89), deg_to_rad(89))
		
		# Fix: Set rotation based on Euler angles. Z-rotation stays at 0 to prevent rolling.
		rotation = Vector3(_pitch, _yaw, 0)

	# Toggle Mouse Capture
	if event.is_action_pressed("ui_cancel"):
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		else:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

	# Click to capture
	if event is InputEventMouseButton and event.pressed:
		if Input.mouse_mode == Input.MOUSE_MODE_VISIBLE:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _process(delta: float) -> void:
	if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
		return

	var current_speed = move_speed
	if Input.is_physical_key_pressed(KEY_SHIFT):
		current_speed = fast_move_speed

	# Input Vector construction
	var input_dir = Vector3.ZERO
	
	if Input.is_physical_key_pressed(KEY_W):
		input_dir += Vector3.FORWARD
	if Input.is_physical_key_pressed(KEY_S):
		input_dir += Vector3.BACK
	if Input.is_physical_key_pressed(KEY_A):
		input_dir += Vector3.LEFT
	if Input.is_physical_key_pressed(KEY_D):
		input_dir += Vector3.RIGHT
	if Input.is_physical_key_pressed(KEY_Q):
		input_dir += Vector3.DOWN
	if Input.is_physical_key_pressed(KEY_E):
		input_dir += Vector3.UP

	input_dir = input_dir.normalized()

	# Movement Logic
	# Since we set rotation cleanly, global_transform.basis is correct automatically.
	var forward = global_transform.basis.z
	var right = global_transform.basis.x
	var up = global_transform.basis.y
	
	var global_dir = (right * input_dir.x) + (up * input_dir.y) + (forward * input_dir.z)
	
	global_position += global_dir * current_speed * delta

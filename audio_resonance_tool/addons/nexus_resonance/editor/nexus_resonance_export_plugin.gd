@tool
extends EditorExportPlugin

## Automatically adds Steam Audio runtime DLLs to Windows exports.
## nexus_resonance.dll depends on phonon.dll, GPUUtilities.dll, TrueAudioNext.dll.
## Windows resolves DLL dependencies from the executable's directory, not from the .pck.


func _get_name() -> String:
	return "NexusResonanceSteamAudio"


func _supports_platform(platform: EditorExportPlatform) -> bool:
	return platform.get_os_name() == "Windows"


func _export_begin(features: PackedStringArray, is_debug: bool, path: String, flags: int) -> void:
	var dlls := PackedStringArray(
		[
			"res://addons/nexus_resonance/bin/phonon.dll",
			"res://addons/nexus_resonance/bin/GPUUtilities.dll",
			"res://addons/nexus_resonance/bin/TrueAudioNext.dll",
		]
	)
	for dll_path in dlls:
		if FileAccess.file_exists(dll_path):
			add_shared_object(dll_path, PackedStringArray(), "")
		else:
			push_warning("Nexus Resonance Export: DLL not found: %s" % dll_path)

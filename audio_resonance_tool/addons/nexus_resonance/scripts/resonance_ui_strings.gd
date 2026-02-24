@tool
extends RefCounted

## Central UI string constants for Nexus Resonance addon. Prep for i18n (translation).

# --- Addon ---
const ADDON_NAME := "Nexus Resonance"
const PREFIX := "Nexus Resonance: "

# --- Buttons ---
const BTN_BAKE_PROBES := "Bake Probes"
const BTN_EXPORT_MESH := "Export Mesh"
const BTN_CLEAR := "Clear"
const BTN_CANCEL := "Cancel"
const BTN_CONTINUE := "Continue"
const BTN_BAKE_ANYWAY := "Bake Anyway"
const BTN_UNDO := "Undo"
const BTN_PREVIEW_BAKE_SETTINGS := "Preview Bake Settings"
const BTN_DOCUMENTATION := "Documentation"

# --- Menu (Project > Tools > Nexus Resonance) ---
const MENU_EXPORT_STATIC_SCENE := "Export Static Scene"
const MENU_EXPORT_DYNAMIC_MESHES := "Export Dynamic Meshes"
const MENU_BAKE_ALL_PROBE_VOLUMES := "Bake All Probe Volumes"
const MENU_CLEAR_PROBE_BATCHES := "Clear Probe Batches"
const MENU_UNLINK_PROBE_VOLUME_REFS := "Unlink Probe Volume References"

# --- Dialogs ---
const DIALOG_BAKE_FAILED_TITLE := "Nexus Resonance - Bake Failed"
const DIALOG_EXPORT_FAILED_TITLE := "Nexus Resonance - Export Failed"
const DIALOG_SAVE_FAILED_TITLE := "Nexus Resonance - Save Failed"
const DIALOG_GDEXTENSION_NOT_LOADED_TITLE := "Nexus Resonance - GDExtension Not Loaded"
const DIALOG_BAKE_VALIDATION_TITLE := "Nexus Resonance - Pre-Bake Check"
const DIALOG_PROGRESS_TITLE := "Nexus Resonance - Baking"
const DIALOG_BACKUP_TITLE := "Nexus Resonance - Backup"
const DIALOG_BACKUP_MESSAGE := "A backup of current Probe Volume data will be created before baking. You can undo the bake to restore the previous data."

# --- Progress ---
const PROGRESS_PREPARING := "Preparing..."
const PROGRESS_PROCESSING := "Processing..."
const PROGRESS_BAKING_REVERB := "Baking reverb probes"
const PROGRESS_BAKING_PATHING := "Baking pathing"
const PROGRESS_BAKING_STATIC_SOURCE := "Baking static source"
const PROGRESS_BAKING_STATIC_LISTENER := "Baking static listener"
const PROGRESS_SKIPPING := "Skipping (up-to-date)"
const PROGRESS_ESTIMATED_TIME := "Estimated time: %s"
const PROGRESS_STAGE := "Stage %d of %d"
const PROGRESS_PROBES := "%d probes"
const PROGRESS_DETAILS := "Details"

# --- Errors (critical = dialog) ---
const ERR_NO_SCENE := "No scene open."
const ERR_GDEXTENSION_NOT_LOADED := "GDExtension not loaded."
const ERR_EXPORT_FAILED := "Export failed with error %s"
const ERR_SCENE_NOT_EXPORTED := "Scene not exported. Use Tools > Nexus Resonance > Export Static Scene before baking."
const ERR_BAKE_RUNNER_NOT_INIT := "Bake runner not initialized."
const ERR_SERVER_LACKS_EXPORT := "ResonanceServer lacks export_static_scene_to_asset. Update the addon."
const ERR_SOFAAsset_UNAVAILABLE := "ResonanceSOFAAsset not available."
const ERR_BAKE_FAILED := "Bake failed. Check Editor output."
const ERR_CONFIG_FAILED := "Failed to build config."
const ERR_FAILED_TO_SAVE := "Failed to save: %s"

# --- Warnings (notification) ---
const WARN_NO_SCENE := "No scene open."
const WARN_NO_DYNAMIC_GEOMETRY := "No ResonanceDynamicGeometry in scene. Nothing to export."
const WARN_SELECT_PROBE_VOLUMES := "Select one or more ResonanceProbeVolume nodes, then run this to unlink references before deleting."
const WARN_NO_PLAYER_REFS := "No ResonancePlayer in this scene points to the selected Probe Volume(s)."
const WARN_NO_PROBE_VOLUMES := "No ResonanceProbeVolume in scene. Nothing to bake."
const WARN_ADD_RUNTIME := "Add ResonanceRuntime with valid ResonanceRuntimeConfig to the scene."
const WARN_SERVER_INIT_FAILED := "Server init failed."
const WARN_BAKE_RUNNER_NOT_SET := "Bake runner not set. Cannot bake."

# --- Info (console) ---
const INFO_STATIC_UNCHANGED := "Static geometry unchanged (hash match). Skipping export."
const INFO_STATIC_EXPORTED := "Static scene exported to %s. ResonanceStaticScene updated."
const INFO_DYNAMIC_EXPORTED := "Dynamic mesh exported to %s"
const INFO_DYNAMIC_MESHES_EXPORTED := "Exported %d dynamic mesh(es)."
const INFO_UNLINK_DONE := "Cleared %d pathing_probe_volume reference(s). You can now delete the Probe Volume(s)."
const INFO_PROBE_BATCHES_CLEARED := "Probe batches cleared."

# --- Tooltips ---
const TT_BAKE_PROBES := "Bake reflections, pathing, static source/listener. Skips up-to-date stages. Configure in bake_config."
const TT_EXPORT_MESH := "Export this dynamic mesh to a .tres asset."
const TT_PREVIEW_BAKE := "Estimate probe count and approximate bake time for current settings."
const TT_HELP := "Open documentation"

# --- Icon paths (res://addons/nexus_resonance/ui/icons/) ---
const ICON_BAKE := "res://addons/nexus_resonance/ui/icons/icon_bake.svg"
const ICON_EXPORT := "res://addons/nexus_resonance/ui/icons/icon_export.svg"
const ICON_CLEAR := "res://addons/nexus_resonance/ui/icons/icon_clear.svg"
const ICON_HELP := "res://addons/nexus_resonance/ui/icons/icon_help.svg"

# --- Docs ---
const DOC_BASE_URL := "https://github.com/nexus-resonance/docs"
const DOC_BAKE_WORKFLOW := DOC_BASE_URL + "#bake-workflow"
const DOC_PROBE_VOLUME := DOC_BASE_URL + "#probe-volume"
const DOC_EXPORT := DOC_BASE_URL + "#export"

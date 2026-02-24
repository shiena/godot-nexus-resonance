# Known Issues

Tracked issues and workarounds for Nexus Resonance. Kept here so they don’t require searching the codebase.

---

## Godot Engine / Editor

### Probe Volume deletion with active references

**Error:**

```
ERROR: core/string/node_path.cpp:272 - Condition "!p_np.is_absolute()" is true. Returning: NodePath()
@Godot Project/scene/test_scene.tscn
```

**When:** Deleting a `ResonanceProbeVolume` that is still referenced by `ResonancePlayer.pathing_probe_volume`.

**Cause:** Godot engine bug in NodePath handling – relative paths trigger `rel_path_to()` to fail when the editor updates references after a node is removed.

**Workaround:**

- References are auto-cleared in `ResonanceProbeVolume._notification(EXIT_TREE)` before Godot’s ref update runs.
- If the error still happens: **Project → Tools → Nexus Resonance → Unlink Probe Volume References** (select the probe volume first), then delete it.
- Or clear `pathing_probe_volume` on all `ResonancePlayer` nodes before deleting the volume.

**Code:**

- `src/resonance_probe_volume.cpp` – `_clear_player_refs_to_this()`
- `addons/nexus_resonance/plugin.gd` – `_on_tool_unlink_probe_refs`

### Dynamic Object Export: Animated mesh states

**When:** Exporting dynamic objects whose mesh geometry changes at runtime.

**Cause:** Export uses the mesh state as it exists in the Editor at export time. The mesh resource from MeshInstance3D is static – no „playing through“ animations is required for export. However, if the visual mesh changes significantly at runtime (e.g., swapped meshes, skeletal deformation), Steam Audio will still use the exported geometry.

**Workaround:** For animated/interactive geometry, use one of:

- Export the „closed“ or default state as the occlusion proxy (typical for doors).
- Use `geometry_override` with a simplified proxy mesh (e.g., BoxMesh) that approximates the acoustic footprint for all states.
- Export multiple assets for different states if each state needs distinct acoustic representation (future extension).

**Code:** `src/resonance_geometry.cpp` – `export_dynamic_mesh_to_asset`, `_create_meshes`

---

## Adding New Entries

Use this template:

```markdown
### [Short title]

**Error:** (if applicable)

**When:** (reproduction steps / situation)

**Cause:** (root cause if known)

**Workaround:** (user-facing fix)

**Code:** (relevant files / functions)
```


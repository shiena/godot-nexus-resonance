# Localization (i18n)

Nexus Resonance currently uses hardcoded English strings. To add translations:

1. Enable Godot's translation system: Project → Project Settings → Localization.
2. Add a CSV or PO file with translation keys.
3. Replace UI strings (toolbar, editor) with `tr("key")` calls.

Key areas for translation:
- Probe toolbar button tooltips
- Error/warning messages in scripts
- Inspector property descriptions (via .translation files for doc classes)

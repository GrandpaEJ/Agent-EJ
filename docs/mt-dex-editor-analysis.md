# MT Manager — Dex Editor Plus (behaviour study)

Observed on MT Manager 2.26.11 (SM-E556B, Android 16) with `ejsample.apk`
(5 dex). This records *what the user sees and does*, so EJ Manager can offer
the same workflow with its own C core. No MT code or libraries are used.

## Entry flow

1. Tap an `.apk` → **APK info dialog**: icon, label, version; package name,
   version code, size, signature scheme (V2), protection ("No Detected"),
   target/min SDK, installed version, data dirs, APK path, first install,
   last update, UID. Buttons: `FUNCTION` · `VIEW` · `INSTALL`.
2. `VIEW` opens the APK as a folder. Tap `classesN.dex` → **Open with…**:
   Dex Editor plus · Dex Editor · Repair dex file · Dex property · Dex2Jar ·
   Dex2Smali · Decrypt dex strings · Translation mode.
3. Dex Editor plus → **MultiDex** checklist of every `classes*.dex` in the
   APK, the tapped one pre-checked. `SELECT ALL` · `CANCEL` · `OK`.
4. Opens a *temporary project* holding all chosen dex merged together.

## Main screen

- Title "Dex Editor plus", subtitle "Temporary project"; ☰ drawer, hammer
  (compile + write back into the APK), ⋮.
- Tabs: **EXPLORER · HISTORY · SEARCH · STRINGS**.
- ⋮: Save as Project · Resource querier · Select dex · Backup · Preferences ·
  Exit. Back needs a double press ("Press again to back").

### Explorer

- One package tree over all dex. Single-child package chains collapse into
  one row (`com.google.common.util.concurrent`, `agentej.android`).
- Inline expand with ˃ / ˩ chevrons, indent per level, packages before
  classes, folder icon for packages, blue round **C** badge for classes.
- Long-press: Copy ▸ · Add · Import · Delete · Rename · Batch operations.

### Search

- "Start a new search" dialog: *Text to find* (with history dropdown),
  *Path* (package scope, `/` = all, + Exclude list), *Search type*
  (Smali · Class name · Field name · Method name · String · Integer),
  ☑ Search subdirectories, ☐ Match case, ☐ Regex.
- Results: header "search results (11) - text", grouped
  package → class → matching line snippet with the hit highlighted.
- After a search: Search in current results · Replace in current results ·
  Clear search results.

### Strings

- Function: Reload · Filter · Replace. Then the raw **string pool** list.

## Class (smali) editor

- First line `# classes3.dex` — the dex the class lives in. Section comments
  such as `# annotations`.
- Syntax colours (directive, type, string, number, comment), line numbers,
  soft wrap with wrap markers, cursor position `1:1` in a tab strip.
- Toolbar: compass (Navigation) · undo · redo · save · pencil (edit) · ⋮.
- Symbol key row above the keyboard: `→ / + - * = <`.
- **Navigation** dialog: outline of the class — C (class), F (field, orange),
  M (method, red) with descriptor, filterable; `SWITCH` flips to saved
  snippets (S); `PREFERENCES`.
- ⋮: Search · Smali to Java · Instruction query · Previous/Next position ·
  Jump to line · Soft wrap ☑ · Read-only ☐ · Smooth mode ☐ ·
  Instruction completion ☑ · Export · Preferences · Close.

## EJ Manager — implementation order (one at a time)

| # | Feature | Core / JNI need | Status |
|---|---------|-----------------|--------|
| 1 | MultiDex picker + merged package tree (collapsed chains, C badges), tap → smali view with `# classesN.dex` | list classes of several dex at once (`dexClasses` per entry) | next |
| 2 | Navigation outline (C/F/M) + jump | parse members from the smali text | |
| 3 | Search tab (6 types, scope, regex, grouped results) | search over class smali / pools in C | |
| 4 | Edit + save a class → reassemble that dex → write back into APK → sign (hammer) | `smali_assemble` per dex, zip replace, sign pipeline | |
| 5 | Strings tab (pool list, filter, replace) | string pool iterator | |
| 6 | History, batch ops, add/import/delete/rename class | | |
| 7 | APK info dialog (FUNCTION/VIEW/INSTALL) | manifest + signature summary | |

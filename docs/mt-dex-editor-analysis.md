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
  M (method, red) with descriptor, filterable; `SWITCH` flips to the
  class's string constants (S, green — the `const-string` values, e.g.
  `getAssets(...)` in MainActivity.onCreate); `PREFERENCES`.
- ⋮: Search · Smali to Java · Instruction query · Previous/Next position ·
  Jump to line · Soft wrap ☑ · Read-only ☐ · Smooth mode ☐ ·
  Instruction completion ☑ · Export · Preferences · Close.

## APK info dialog (tap an .apk)

- Two-column table: Package name · Version code · File size (`29.49M`) ·
  Signature (`V1 + V2 + V3`) · Protection (`No Detected`) · Target / Minimum
  SDK (`Android 16 (API 36)`) · Installed · Data directory 1 / 2 · APK path ·
  First install · Last update · UID.
- Tap a row: copies the value ("Text has been copied to the clipboard"),
  except Signature → *Signature information* (File, Scheme, Status, Algorithm,
  Public key › {algorithm, size, exponent, modulus}, Valid from/until, Owner,
  HASH, CRC32, MD5, SHA1, SHA256, Format: Add colon / Upper case;
  VIEW DATA = cert DER in the hex viewer, COMPARE = "Paste and Verify"), and
  Data directory / APK path → open that folder in the pane.

## resources.arsc — Translation mode

- Tap `resources.arsc` → Open with: Arsc Editor plus · Arsc Editor ·
  **Translation mode** · Resource querier.
- First screen: every configuration of the string type as its raw
  qualifier, sorted — `[DEFAULT]`, `af`, `am`, … `b+es+419`, `en-rGB`.
  ⚙ Preferences: ☑ "Export the translation data to arsc when saving file".
  ⋮: Save · Dictionary manager · Generate dictionary · Exit.
- Pick a language → that language's strings, counter `translated/total`
  (`0/746`). Left: the language's current text; right: the new text, empty
  until edited. The language is edited in place.
- Tap a row → dialog titled with the key: current text, then a
  "Translation" field prefilled with it (selected). CANCEL · OK.
- Bottom bar: select all · select · + · globe (online translate) · list.
- Save keeps the session's edits; leaving asks "Do you want to save the
  file?" — EXIT DIRECTLY · SAVE AND EXIT, which writes the arsc.

EJ Manager's take (`TranslateScreen`): opens on the device language when the
table has it; each row shows key, default text and the chosen language's
text ("Not translated" when missing), so the default is the reference while
translating; All / Untranslated / Edited filters and search; languages by
name with "Add language" (`-ll` / `-ll-rCC`); ✓ writes a signed
`<name>_edited.apk` and later saves build on it. Not built yet: dictionaries,
online translate, multi-select.

## EJ Manager — implementation order (one at a time)

| # | Feature | Core / JNI need | Status |
|---|---------|-----------------|--------|
| 1 | MultiDex picker + merged package tree (collapsed chains, C badges), tap → smali view with `# classesN.dex` | list classes of several dex at once (`dexClasses` per entry) | done |
| 2 | Navigation outline (C/F/M, switch to S strings) + jump | parse members from the smali text | done |
| 3 | Search tab (6 types, scope, regex, grouped results) | `dex_find` in C (prefilter, render candidates only) | done |
| 4 | Edit + save a class → reassemble that dex → write back into APK → sign (hammer) | `smali_patch_dex` (render all, swap edited, assemble one dex), `apk_replace_entries`, sign pipeline → `<name>_edited.apk` | done (small dex verified; 18 MB dex untimed) |
| 5 | Strings tab (pool list, filter, replace) | `dex_string_count`/`dex_string_at`, `dexStrings`; find uses via `dex_find`; replace rewrites literals into editor edits | done (phone: filter, copy, find uses, replace → hammer verified) |
| 6 | History, batch ops, add/import/delete/rename class | `smali_rename` (class/package descriptors over all classes), deletes = skipped class with no text in `smali_patch_dex`; History tab; long-press menu on class/package (copy, add, import .smali, rename, delete) | done (phone: rename, delete, add, history → hammer verified; import untested). MT's multi-select batch mode not built — package rename/delete cover the batch cases |
| 7 | APK info dialog (FUNCTION/VIEW/INSTALL) | `apk_sig_schemes` (signing block IDs: V1/V2/V3/V3.1/stamp); PackageManager for manifest, installed copy, signer cert; packer markers for Protection | done (phone: table matches MT on the same APK; Signature → cert info/public key/hashes/VIEW DATA hex/COMPARE; data dir and APK path rows open the folder; other rows copy) |

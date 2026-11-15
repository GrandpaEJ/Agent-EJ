# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [New Released]

## [0.7.0] - 2026-11-15

### Added
- EJ workspace folder (`/sdcard/EJ`, drawer entry): decoded projects go to `EJ/projects`, output for read-only sources to `EJ/output`
- Dex editor: edit a class and compile with the hammer; every touched dex is rebuilt and written into a signed `<name>_edited.apk` (or `_edited.dex`), with a prompt on leaving with uncompiled changes
- Dex editor Strings tab: the string pool of every open dex with a filter; tap a string to copy it, find its uses or replace it in every class that uses it
- Dex editor History tab and long-press menu on classes and packages: copy name/descriptor, add a class, import a `.smali`, rename (references in the open dex follow) and delete
- APK info dialog on tapping an APK, laid out like MT: package, version, size, signature schemes, protection, SDKs, installed copy, data directories, install dates, UID; FUNCTION / VIEW / INSTALL
- Signature information: scheme, status, algorithm, public key, validity, owner, HASH/CRC32/MD5/SHA1/SHA256 with format switches, certificate bytes in the hex viewer, paste-and-verify a hash

### Fixed
- The drawer shows the real core version (read from CHANGELOG.md at build time)


## [0.6.0] - 2026-11-15

### Added
- MT-style dual-pane commander: two panes with their own history and `..` rows, compact rows, the bars follow the active pane, swap/copy/move to the other pane
- Tap opens known types (text, xml, image, archive, dex) and asks for unknown ones; long-press grid menu; open-with / share through a FileProvider
- Tools on APK, dex, AXML, ARSC and decoded projects via the native core (decode, build, sign, repair, kill signature check, merge, dex→smali, deobfuscate); outputs are written beside the input with a SHOW action
- Hex and image viewers
- Dex editor: MultiDex picker, one package tree over every dex (single-child chains folded), classes open headed by their dex; one native dex session at a time
- Dex editor Navigation: class, fields and methods (or the string constants) with jump-to-line
- Dex editor Search tab: smali, class, field, method, string or integer search over all dex with package scope, case and regex; grouped results open at the hit
- Back steps back through the pane history, then up a folder; a second Back within 2 s closes the app

### Fixed
- Building a decoded APK no longer drops dex contents (core `.param` fix)


## [0.5.0] - 2026-11-15

### Added
- Thumbnails for images (incl. inside archives), APK icons, video frames, audio covers and PDF pages; grid view; memory- and disk-bounded thumbnail cache


## [0.4.0] - 2026-11-15

### Added
- File manager pass: long-press actions, multi-select copy/cut/paste/rename/delete, new file/folder, sort and hidden toggle, filter + recursive search, drawer with storage and bookmarks, back/forward/up bottom bar, code viewer (line numbers, smali/xml/json colour, find, wrap, edit), contrast-checked token palette; streaming recursive copy and cross-fs move in C


## [0.3.0] - 2026-11-15

### Added
- UI pass against MT Manager: two-line rows with date+size, colour type tiles, Folders/Files/Disk header, in-archive browsing, APK info tab (package, sdk, signature, SHA-256), diskStats and fs-write tools in C


## [0.2.0] - 2026-11-15

### Added
- EJ Manager Android app: Compose UI over the C core (file browser, APK entry tooling, AXML/ARSC decoding, DEX-smali, extract/repack/zipalign/sign)

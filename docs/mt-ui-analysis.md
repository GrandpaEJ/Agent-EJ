# MT Manager — UI reference analysis

Captured on the test phone (Samsung M55, 1080×2400 @ 450 dpi, density 2.8125)
while walking MT Manager 2.26.11. Every number below is measured from a
full-resolution screenshot, not estimated.

## 1. Palette (sampled with PIL)

| Role | Hex | Notes |
|---|---|---|
| Top bar / drawer header | `#151515` | near-black, no elevation shadow |
| List / surface | `#303030` | the dominant field behind rows |
| Divider | `#2A2A2A` 1 px | barely visible, rows read as a single block |
| Primary text | `#C5C5C5` – `#CDCDCD` | grey-white, never `#FFFFFF` |
| Secondary text | `#898989` | dates, sizes, counts |
| Dex icon fill | `#357476` | teal |
| ARSC icon fill | `#985E00` | amber |
| XML icon fill | `#314D9D` | blue |
| Action links | `#4C8DFF` | FUNCTION / VIEW / INSTALL |

Take-away: MT is a *neutral grey* product. No brand accent belongs to chrome,
only to file-type glyphs and link text. Everything else is a value step of grey.

## 2. Metrics (real px → dp at 2.8125)

| Element | px | dp |
|---|---|---|
| File row pitch | 112 | ~40 |
| Leading icon tile | 72 | ~26 |
| Row left inset | 40 | ~14 |
| Name / meta gap | ~30 | ~11 |
| Top bar | 232 | ~82 (title 18sp + sub 12sp) |
| Bottom action bar | 144 | ~51 |

Row = **two lines**: name (≈15sp) then `date  size` (≈11sp) in `#898989`.
No separators between file rows in the archive view — the pitch alone
separates them; separators appear only between sections.

## 3. Screens reviewed

### 3.1 File browser (dual pane)
- Split in the middle; each pane has the **same** header shape:
  hamburger → full path (18sp) → overflow, then a stats line
  `Folders: 16  Files: 3  Disk: 53.02G/222.92G` in `#898989`.
- Bottom bar is shared by both panes: `‹ ›  +  ⇄  ↑` (back, forward, new/import,
  swap panes, up). Icons only, evenly spaced, 51 dp tall, `#151515`.
- Row: 26 dp coloured square (folder = grey, apk = app icon, image = thumbnail)
  + name + meta line `26-11-15 10:32  10.22M`.
- Nothing is cropped: names ellipsis at the pane edge and the meta line keeps
  the size right after the date.

### 3.2 Drawer
- Header: app icon, "MT Manager" + "Not logged in" (10sp) + two 24 dp icons.
- Sections `Local` and `Tools` with 12sp grey labels and a chevron, separated
  by 1 px rules.
- Rows: 28 dp circular icon badge on `#303030`, label 16sp, 56 dp pitch.
- Storage rows carry a 3 px progress bar under the text plus
  `6.17G used, 0B available` / `53.01G used, 169.90G available`.
- VIP row is a filled `#E5C15A` banner — the only saturated chrome in the app.

### 3.3 APK info sheet (tap an APK)
- Card: icon 56 dp + name 17sp + version 12sp grey.
- Body is a **two-column table**, left column `#898989` labels, right column
  values in `#CDCDCD`, 34 dp pitch, right column ellipsised with `…`.
  Rows: Package name, Version code, File size, Signature, Protection,
  Target SDK, Minimum SDK, Installed, Data directory 1/2, APK path,
  First install, Last update, UID.
- Footer: left-aligned link `FUNCTION`, right-aligned `VIEW` and `INSTALL`,
  all `#4C8DFF`, 13sp, uppercase.

### 3.4 Inside an archive
- An APK is not a modal screen: the pane **navigates into it**
  (`ejsample.apk/` in the header, stats become `Folders: 5  Files: 8`).
- Entries get coloured tiles by type: folders grey, `.xml` blue `#314D9D`,
  `.dex` teal `#357476`, `.arsc` amber `#985E00`, everything else grey.
- Sizes are shown with the zip date `81-01-01 01:01` and a compact size
  (`17.29M`) — i.e. exactly the same row layout as the filesystem.

### 3.5 Function sheet (form pattern)
- Label 13sp `#898989` above value 16sp `#CDCDCD`, 1 px underline per field.
- Toggle row: label left, Material switch right (blue when on).
- Disclaimer paragraph 11sp `#898989`.
- Footer: checkbox `AUTO SIGN` left, `CANCEL` + `OK` links right.

## 4. What we adopt in EJ Manager

1. **Two-line rows** everywhere: name + `date  size` meta, ~44 dp pitch.
2. **Square colour tiles** instead of emoji: grey folder, teal dex,
   amber arsc, blue xml, green apk.
3. **Header with counts**: `Folders: N  Files: M  Disk: used/total`
   (needs `statvfs`, so it comes from C like every other fact).
4. **Archive browsing inside the browser** — tapping an APK descends into
   `x.apk/…` instead of opening a separate modal, with a breadcrumb back.
5. **APK info as a two-column table** on the APK screen, values from C
   (`package`, `versionName/Code`, `minSdk/targetSdk`, size, signature scheme,
   SHA-256, entry count).
6. Keep our dark palette (`#0E1013`/`#14171C`) but tighten the greys toward
   MT's steps: primary `#DDE1E6`, secondary `#8B9098`, hairline `#24282F`.
   Brand green stays for state/success only — chrome goes grey.

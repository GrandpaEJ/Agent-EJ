# AgentX MT — Android app

A native APK toolkit for Android, built on the Agent-X C core. Think MT Manager,
minus the agent: **Kotlin draws the UI, C does every operation.**

```
android/
├── app/src/main/cpp/            # libejcore.so  — all real work
│   ├── ej_jni_fs.c              # listDir, stat, read/write, mkdir, delete, rename, copy
│   ├── ej_jni_zip.c             # zip entries, read entry, extract, repack tree, zipalign
│   ├── ej_jni_axml.c            # binary XML -> text, text -> binary XML, axml repair
│   ├── ej_jni_arsc.c            # string pool listing, in-place string patch, TOML dump
│   ├── ej_jni_dex.c             # class list, class -> smali, dex dump, smali -> dex
│   ├── ej_jni_apk.c             # apk analysis, v1 -> align -> v2/v3 signing, repair
│   └── ej_jni_util.c            # JNI marshalling + one-time bridge context
├── app/src/main/java/org/agentej/android/
│   ├── Native.kt                # the whole native surface (externals only)
│   ├── Util.kt                  # formatting helpers (no I/O)
│   ├── MainActivity.kt          # storage gate + navigation stack
│   └── ui/                      # Compose screens: browser, apk editor, viewer
└── app/src/main/assets/keys/    # shipped self-signed debug key for "Sign"
```

## Division of labour

| Layer | Owns |
|-------|------|
| Kotlin | Compose rendering, navigation, `MANAGE_EXTERNAL_STORAGE` intent, coroutines |
| C (`libejcore.so`) | every filesystem call, ZIP, AXML, ARSC, DEX, Smali, signatures (SHA-1/256, RSA, PKCS#7) |

There is no `java.io.File` walking, no `ZipInputStream`, no Java cryptography:
the browser lists directories through `opendir`/`stat`, the APK tab reads the
central directory itself, and signing is the C pipeline
`apk_sign_v1 → zipalign → apk_sign_v2_v3`.

## Features (v1, non-agentic)

- Dual-purpose file browser over `/sdcard` and app storage, dirs-first sorting
- ZIP/APK entry listing with compressed size + method
- `AndroidManifest.xml` binary XML decoded to text (and re-encodable)
- `resources.arsc` string pool listing, in-place string patch, TOML dump
- DEX class list read straight from the header tables; tap a class for smali
- Extract APK, repack an unpacked tree, zipalign, sign (v1+v2+v3, built-in key)
- Binary resource repair for dex / arsc / axml

## Build

```bash
cd android
gradle :app:assembleDebug          # needs ANDROID_HOME + NDK 27.2.12479018
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell appops set org.agentej.android MANAGE_EXTERNAL_STORAGE allow
```

`local.properties` (SDK path) is machine-local and ignored by git.

## Verified on device

| Check | Result |
|-------|--------|
| Browser listing | 18 items in `/storage/emulated/0` |
| APK entries | 115 entries from a 10 MB APK |
| Manifest | AXML decoded to readable XML |
| ARSC | string pool listed |
| DEX | 5 dex files, class list + smali (`invoke-direct`, `.line`, annotations) |
| Extract | 115 files written to `*_unpacked/` |
| Sign | `apksigner verify -v` → v2 `true`, v3 `true`, zipalign `-c 4` OK |

## Native targeting notes

- `-Wl,-z,max-page-size=16384` so the .so loads on 16 KB-page devices (Android 15+)
- `ANDROID_STL=none`: the core is plain C11 + bionic, no C++ runtime
- `src/tools`, `src/net`, `src/core`, `src/android/adb` are excluded from the
  .so — they pull in cJSON, sockets and the agent loop; the app only links the
  format/crypto core

# Agent-EJ CLI Tools Guide

Agent-EJ provides a powerful set of built-in tools that are heavily utilized by the autonomous AI, but are also fully accessible via the Command Line Interface (CLI). 

These tools cover File System operations, System level executions, Network integrations, and advanced Android/APK Reverse Engineering tasks.

---

## 1. File System Tools (`tool_fs.c`)

### `read_file`
Reads the content of a specified file.
```bash
./agent-ej tool read_file path="/path/to/file.txt"
```

### `write_file`
Writes or overwrites content into a file.
```bash
./agent-ej tool write_file path="/path/to/file.txt" content="Hello World"
```

### `list_dir`
Lists all files and subdirectories in a directory.
```bash
./agent-ej tool list_dir path="/path/to/dir"
```

### `delete_file`
Deletes a file or recursively removes a directory.
```bash
./agent-ej tool delete_file path="/path/to/remove"
```

### `search_file`
Searches for a specific filename within a directory tree.
```bash
./agent-ej tool search_file path="/search/dir" name="target.txt"
```

---

## 2. System Tools (`tool_sys.c`)

### `run_command`
Executes a raw shell command. (Requires sandbox/safety checks depending on environment).
```bash
./agent-ej tool run_command cmd="ls -la /tmp"
```

### `dynamic_skill`
Executes a dynamically loaded script or binary skill from the `skills/` directory.
```bash
./agent-ej tool dynamic_skill name="my_script.sh"
```

---

## 3. Network Tools (`tool_net.c`)

### `download_file`
Downloads a file from a URL to the local filesystem.
```bash
./agent-ej tool download_file url="https://example.com/file.zip" path="/tmp/file.zip"
```

---

## 4. Android Debug Bridge (ADB) Tools (`tool_adb.c`)

Agent-EJ implements its own ADB protocol client to communicate directly with ADB daemons (`adbd`) over TCP/IP or localhost, bypassing the need for the external `adb` binary.

### `adb_devices`
Lists all connected Android devices/emulators.
```bash
./agent-ej tool adb_devices
```

### `adb_shell`
Executes a shell command directly on the connected Android device.
```bash
./agent-ej tool adb_shell cmd="dumpsys battery"
```

### `adb_push` / `adb_pull`
Transfers files to and from the Android device.
```bash
./agent-ej tool adb_push local="/tmp/file.txt" remote="/sdcard/file.txt"
./agent-ej tool adb_pull remote="/sdcard/file.txt" local="/tmp/file.txt"
```

### `adb_install` / `adb_uninstall`
Installs or uninstalls an APK on the device.
```bash
./agent-ej tool adb_install path="/tmp/app.apk"
./agent-ej tool adb_uninstall package="com.example.app"
```

---

## 5. APK & Reverse Engineering Tools (`tool_apk.c`, `tool_dex.c`, `tool_axml.c`)

### `decode_apk`
Fully decompiles an APK, producing `AndroidManifest.xml` and `smali` directories.
```bash
./agent-ej tool decode_apk path="target.apk" out_dir="/tmp/out"
```

### `build_apk`
Re-compiles smali files, packages the APK, zip-aligns, and signs it.
```bash
./agent-ej tool build_apk src_dir="/tmp/out" out_apk="new.apk" key_path="testkey.pem"
```

### `analyze_apk`
Provides a structural overview of the APK without extracting it fully to disk.
```bash
./agent-ej tool analyze_apk path="target.apk"
```

### `read_axml` / `axml_assemble`
Decodes binary `AndroidManifest.xml` into readable XML, or compiles standard XML back into binary.
```bash
./agent-ej tool read_axml path="AndroidManifest.xml" out_dir="/tmp/"
./agent-ej tool axml_assemble in_xml="AndroidManifest.xml" out_bin="OutManifest.xml"
```

### `disasm_dex` / `smali_assemble` / `read_dex`
Raw DEX-to-Smali and Smali-to-DEX operations.
```bash
./agent-ej tool smali_assemble src_dir="smali/" out_dex="classes.dex"
./agent-ej tool read_dex path="classes.dex" out_dir="smali/"
```

### `resign_apk`
Signs an existing ZIP/APK file natively.
```bash
./agent-ej tool resign_apk path="app.apk" key="key.pem" cert="cert.pem"
```

### `zipalign`
Aligns uncompressed resources inside a ZIP/APK to 4-byte boundaries to reduce memory mapping overhead on Android.
```bash
./agent-ej tool zipalign in="app.apk" out="aligned.apk" align=4
```

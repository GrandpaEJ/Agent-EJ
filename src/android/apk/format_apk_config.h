#ifndef FORMAT_APK_CONFIG_H
#define FORMAT_APK_CONFIG_H

#include <stddef.h>
#include <stdint.h>

// Build an Android resource qualifier string (e.g. "-land-xhdpi-v26") from a
// RES_TABLE_TYPE_TYPE chunk (0x0201). The ResTable_config struct starts at
// chunk+20 (8-byte ResChunk_header + 12-byte ResTable_type fixed fields).
// Emits segments in AOSP ResTable_config::toString() order. On success out
// starts with '-' (empty string for the default config); always NUL-terminated.
void apk_build_config_str(const uint8_t *chunk, char *out, size_t out_sz);

#endif

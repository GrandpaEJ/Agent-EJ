#include "format_apk_config.h"
#include "../arsc/format_arsc_internal.h"
#include <stdio.h>
#include <string.h>

// AOSP ResTable_config layout (androidfw/ResourceTypes.h), little-endian.
// The config struct follows the 8-byte ResChunk_header and the 12-byte
// ResTable_type fixed fields of a RES_TABLE_TYPE_TYPE chunk, i.e. it starts
// at chunk+20. Its `size` field bounds every field read below. Offsets were
// verified byte-for-byte against `aapt2 dump chunks` output.
#define CFG_HDR_SIZE  20u   // bytes before the config struct in a type chunk
#define CFG_MIN_SIZE  28u   // smallest complete legacy ResTable_config

static void qadd(char *buf, size_t buf_sz, const char *seg) {
    size_t len = strlen(buf);
    size_t seg_len = strlen(seg);
    if (len + 1 + seg_len + 1 > buf_sz) return;
    if (len) buf[len++] = '-';
    memcpy(buf + len, seg, seg_len + 1);
}

static void qaddf(char *buf, size_t buf_sz, const char *fmt, unsigned v) {
    char seg[32];
    snprintf(seg, sizeof(seg), fmt, v);
    qadd(buf, buf_sz, seg);
}

static const char *density_str(uint16_t d) {
    switch (d) {
        case 120:   return "ldpi";
        case 160:   return "mdpi";
        case 213:   return "tvdpi";
        case 240:   return "hdpi";
        case 320:   return "xhdpi";
        case 480:   return "xxhdpi";
        case 640:   return "xxxhdpi";
        case 0xFFFE: return "anydpi";
        case 0xFFFF: return "nodpi";
        default:    return NULL;   // print as "<N>dpi"
    }
}

void apk_build_config_str(const uint8_t *chunk, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!chunk) return;

    // Validate the chunk header can hold a complete legacy config...
    uint16_t hdr_size = arsc_r16(chunk + 2);
    if (hdr_size < CFG_HDR_SIZE + CFG_MIN_SIZE) return;

    // ...then trust only what the config's own size field declares.
    const uint8_t *cfg = chunk + CFG_HDR_SIZE;
    uint32_t cfg_size = arsc_r32(cfg);
    if (cfg_size < CFG_MIN_SIZE || hdr_size < CFG_HDR_SIZE + cfg_size) return;

    uint8_t  orientation  = cfg[12];
    uint16_t density      = arsc_r16(cfg + 14);
    uint8_t  input_flags  = cfg[18];
    uint16_t sdk          = arsc_r16(cfg + 24);
    uint8_t  screen_layout = cfg[28];
    uint8_t  ui_mode      = cfg[29];
    uint16_t sw_dp        = arsc_r16(cfg + 30);
    uint16_t w_dp         = (cfg_size >= 36) ? arsc_r16(cfg + 32) : 0;
    uint16_t h_dp         = (cfg_size >= 36) ? arsc_r16(cfg + 34) : 0;
    uint8_t  screen_layout2 = (cfg_size >= 50) ? cfg[48] : 0;
    uint8_t  color_mode   = (cfg_size >= 50) ? cfg[49] : 0;

    // Qualifier order matches AOSP ResTable_config::toString().
    char buf[160] = "";

    switch (screen_layout & 0xC0) {            // layout direction
        case 0x40: qadd(buf, sizeof(buf), "ldltr"); break;
        case 0x80: qadd(buf, sizeof(buf), "ldrtl"); break;
        default: break;
    }
    if (sw_dp) qaddf(buf, sizeof(buf), "sw%udp", sw_dp);
    if (w_dp)  qaddf(buf, sizeof(buf), "w%udp", w_dp);
    if (h_dp)  qaddf(buf, sizeof(buf), "h%udp", h_dp);
    switch (screen_layout & 0x0F) {            // screen size
        case 1: qadd(buf, sizeof(buf), "small"); break;
        case 2: qadd(buf, sizeof(buf), "normal"); break;
        case 3: qadd(buf, sizeof(buf), "large"); break;
        case 4: qadd(buf, sizeof(buf), "xlarge"); break;
        default: break;
    }
    switch (screen_layout & 0x30) {            // screen aspect
        case 0x10: qadd(buf, sizeof(buf), "long"); break;
        case 0x20: qadd(buf, sizeof(buf), "notlong"); break;
        default: break;
    }
    switch (screen_layout2 & 0x03) {           // round screen
        case 0x01: qadd(buf, sizeof(buf), "notround"); break;
        case 0x02: qadd(buf, sizeof(buf), "round"); break;
        default: break;
    }
    switch (color_mode & 0x03) {               // wide color gamut
        case 0x01: qadd(buf, sizeof(buf), "nowidecg"); break;
        case 0x02: qadd(buf, sizeof(buf), "widecg"); break;
        default: break;
    }
    switch (color_mode & 0x0C) {               // HDR
        case 0x04: qadd(buf, sizeof(buf), "lowdr"); break;
        case 0x08: qadd(buf, sizeof(buf), "highdr"); break;
        default: break;
    }
    switch (orientation) {                     // orientation
        case 1: qadd(buf, sizeof(buf), "port"); break;
        case 2: qadd(buf, sizeof(buf), "land"); break;
        default: break;
    }
    switch (ui_mode & 0x0F) {                  // UI mode type
        case 2: qadd(buf, sizeof(buf), "desk"); break;
        case 3: qadd(buf, sizeof(buf), "car"); break;
        case 4: qadd(buf, sizeof(buf), "television"); break;
        case 5: qadd(buf, sizeof(buf), "appliance"); break;
        case 6: qadd(buf, sizeof(buf), "watch"); break;
        case 7: qadd(buf, sizeof(buf), "vrheadset"); break;
        default: break;
    }
    switch (ui_mode & 0x30) {                  // night mode
        case 0x10: qadd(buf, sizeof(buf), "notnight"); break;
        case 0x20: qadd(buf, sizeof(buf), "night"); break;
        default: break;
    }
    if (density != 0) {                        // density
        const char *dstr = density_str(density);
        if (dstr) qadd(buf, sizeof(buf), dstr);
        else qaddf(buf, sizeof(buf), "%udp", density);
    }
    switch (cfg[13]) {                         // touchscreen
        case 1: qadd(buf, sizeof(buf), "notouch"); break;
        case 2: qadd(buf, sizeof(buf), "stylus"); break;
        case 3: qadd(buf, sizeof(buf), "finger"); break;
        default: break;
    }
    switch (input_flags & 0x03) {              // keyboard availability
        case 1: qadd(buf, sizeof(buf), "keysexposed"); break;
        case 2: qadd(buf, sizeof(buf), "keyshidden"); break;
        case 3: qadd(buf, sizeof(buf), "keyssoft"); break;
        default: break;
    }
    switch (cfg[16]) {                         // keyboard
        case 1: qadd(buf, sizeof(buf), "nokeys"); break;
        case 2: qadd(buf, sizeof(buf), "qwerty"); break;
        case 3: qadd(buf, sizeof(buf), "12key"); break;
        default: break;
    }
    switch (input_flags & 0x0C) {              // navigation availability
        case 0x04: qadd(buf, sizeof(buf), "navexposed"); break;
        case 0x08: qadd(buf, sizeof(buf), "navhidden"); break;
        default: break;
    }
    switch (cfg[17]) {                         // navigation
        case 1: qadd(buf, sizeof(buf), "nonav"); break;
        case 2: qadd(buf, sizeof(buf), "dpad"); break;
        case 3: qadd(buf, sizeof(buf), "trackball"); break;
        case 4: qadd(buf, sizeof(buf), "wheel"); break;
        default: break;
    }
    if (sdk != 0) qaddf(buf, sizeof(buf), "v%u", sdk);   // version: always last

    if (buf[0]) snprintf(out, out_sz, "-%s", buf);
}

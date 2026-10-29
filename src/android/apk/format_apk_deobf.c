/* APK-wide deobfuscation driver (repair pass): copies an APK entry by entry,
 * replacing each tampered DEX, binary XML and resource table with its repaired
 * form and leaving everything else byte-for-byte. The result is a standalone
 * APK that jadx / apktool / aapt2 can open; it is left unsigned and aligned
 * (the caller signs it if an installable APK is wanted). */
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARSC_NAME     "resources.arsc"
#define MANIFEST_NAME "AndroidManifest.xml"

static int ends_with(const char *s, const char *suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}
static int is_binary_xml(const uint8_t *d, size_t sz) {
    return sz >= 8 && d[0] == 0x03 && d[1] == 0x00;
}

/* Page-aligns shared libraries, 4-aligns the rest (mmap-friendly at runtime). */
static void add_entry(zip_writer *zw, const char *name, const void *data, size_t sz) {
    size_t l = strlen(name);
    int align = (l > 3 && strcmp(name + l - 3, ".so") == 0) ? 4096 : 4;
    zip_writer_add(zw, name, data, sz, 0, align);
}

/* Appends "name: detail" to the running report, never overflowing it. */
static void report_line(char *rep, size_t cap, const char *name, const char *detail) {
    if (!rep || !cap) return;
    size_t len = strlen(rep);
    if (len + 1 >= cap) return;
    snprintf(rep + len, cap - len, "%s%s: %s", len ? "\n" : "", name, detail);
}

int apk_deobf_repair(const char *in_apk, const char *out_apk,
                     char *report, size_t report_cap) {
    if (report && report_cap) report[0] = '\0';
    zip_archive *za = zip_open(in_apk);
    if (!za) return -1;
    zip_writer *zw = zip_writer_open(out_apk);
    if (!zw) { zip_close(za); return -1; }

    int n = zip_get_num_entries(za), fixed = 0;
    char changes[256];
    for (int i = 0; i < n; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name) continue;
        size_t l = strlen(name);
        if (l && name[l - 1] == '/') continue;     /* directory marker */

        size_t sz = 0;
        uint8_t *data = zip_extract_entry(za, i, &sz);
        if (!data) continue;

        uint8_t *rep_data = NULL;
        size_t rep_sz = 0;
        if (ends_with(name, ".dex")) {
            rep_data = dex_repair(data, sz, &rep_sz, changes, sizeof(changes));
        } else if (strcmp(name, ARSC_NAME) == 0) {
            rep_data = arsc_repair(data, sz, &rep_sz, changes, sizeof(changes));
        } else if ((strcmp(name, MANIFEST_NAME) == 0 || ends_with(name, ".xml")) &&
                   is_binary_xml(data, sz)) {
            rep_data = axml_repair(data, sz, &rep_sz, changes, sizeof(changes));
        }

        if (rep_data) {
            add_entry(zw, name, rep_data, rep_sz);
            report_line(report, report_cap, name, changes);
            if (strstr(changes, "no tampering found") == NULL) fixed++;
            free(rep_data);
        } else {
            add_entry(zw, name, data, sz);         /* copied unchanged */
        }
        free(data);
    }
    zip_close(za);
    int rc = zip_writer_close(zw);
    if (rc == 0 && report && report_cap && report[0] == '\0')
        snprintf(report, report_cap, "nothing to repair");
    return rc == 0 ? fixed : -1;
}

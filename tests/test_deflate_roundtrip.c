/* Deflates each file given, inflates it back with our inflater, and writes
 * the raw stream to <file>.dfl so zlib can check it too. */
#include "../src/formats/zip/format_zip_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { printf("FAIL open %s\n", argv[a]); bad++; continue; }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *in = malloc((size_t)n + 1);
        size_t got = fread(in, 1, (size_t)n, f);
        fclose(f);
        size_t cn = 0, un = 0;
        uint8_t *c = zip_deflate(in, got, &cn);
        uint8_t *u = c ? zip_inflate(c, cn, &un) : NULL;
        int ok = u && un == got && memcmp(u, in, got) == 0;
        printf("%s %s %ld -> %zu (%.1f%%)\n", ok ? "ok  " : "FAIL", argv[a], n, cn, n ? 100.0 * (double)cn / (double)n : 0.0);
        bad += !ok;
        char out[4096];
        snprintf(out, sizeof(out), "%s.dfl", argv[a]);
        if (c && (f = fopen(out, "wb"))) { fwrite(c, 1, cn, f); fclose(f); }
        free(in); free(c); free(u);
    }
    return bad != 0;
}

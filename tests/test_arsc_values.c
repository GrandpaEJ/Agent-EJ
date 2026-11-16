/* arsc_tr over non-string values: dump a type, set a value, encode.
 * usage: test_arsc_values in.arsc type [id qual text]... out.arsc */
#include "formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)l);
    *n = fread(b, 1, (size_t)l, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    size_t n = 0;
    uint8_t *d = slurp(argv[1], &n);
    arsc_tr *t = d ? arsc_tr_open(d, n) : NULL;
    if (!t) { puts("cannot open"); return 1; }
    char *dump = arsc_tr_dump_of(t, argv[2], "");
    fputs(dump, stdout);
    free(dump);
    int i = 3;
    for (; i + 3 < argc; i += 3) {
        int r = arsc_tr_set(t, (uint32_t)strtoul(argv[i], NULL, 16), argv[i + 1], argv[i + 2]);
        printf("set %s %s '%s' -> %d\n", argv[i], argv[i + 1], argv[i + 2], r);
    }
    size_t on = 0;
    uint8_t *o = arsc_tr_encode(t, &on);
    FILE *f = fopen(argv[i], "wb");
    if (!o || !f) return 1;
    fwrite(o, 1, on, f);
    fclose(f);
    free(o); free(d);
    arsc_tr_free(t);
    return 0;
}

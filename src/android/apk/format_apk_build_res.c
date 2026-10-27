/* Resource recompilation step of the APK rebuild.
 *
 * decode_apk turns resources.arsc into a res values tree of XML files, but
 * build_apk then packed whatever resources.arsc happened to be on disk, so an
 * edit made in one of those files never reached the rebuilt APK.  This is the
 * missing half: before the tree is packed, the table is regenerated from it.
 *
 * Entries the tree does not describe keep their original value, so a rebuild
 * of an untouched decode reproduces the original table byte for byte.  A
 * failure is not fatal: the original table is packed instead and the caller is
 * told, which keeps a rebuild working for trees this compiler cannot read.
 */

#include "formats.h"
#include <stdio.h>
#include <sys/stat.h>

int apk_recompile_resources(const char *src_dir) {
    char arsc_path[4096], res_path[4096], tmp_path[4112];
    struct stat st;

    snprintf(arsc_path, sizeof(arsc_path), "%s/resources.arsc", src_dir);
    snprintf(res_path, sizeof(res_path), "%s/res", src_dir);
    if (stat(arsc_path, &st) != 0) return 0;   /* nothing to recompile */
    if (stat(res_path, &st) != 0) return 0;    /* no decoded values tree */

    /* Compiled beside the original and renamed over it, so a half written
     * table can never be picked up by the pack step. */
    snprintf(tmp_path, sizeof(tmp_path), "%s.recompiled", arsc_path);

    int changed = 0, skipped = 0;
    if (arsc_compile_xml(arsc_path, res_path, tmp_path, &changed, &skipped) != 0) {
        remove(tmp_path);
        printf("resources.arsc: could not compile the res values tree, "
               "packing the original table\n");
        return -1;
    }
    if (rename(tmp_path, arsc_path) != 0) {
        remove(tmp_path);
        printf("resources.arsc: could not replace the table, "
               "packing the original one\n");
        return -1;
    }
    printf("resources.arsc: %d value(s) compiled from res, "
           "%d left as they were\n", changed, skipped);
    return 0;
}

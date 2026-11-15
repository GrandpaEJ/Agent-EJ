/* Filesystem copy and move. Files stream through one 64 KB stack buffer, so
 * copying a 2 GB OBB costs the same RSS as copying a 2 KB manifest. */
#include "ej_jni_util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define EJ_COPY_BUF (64 * 1024)
#define EJ_COPY_DEPTH 64

static int copy_file(const char *from, const char *to, const struct stat *st) {
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st->st_mode & 0777);
    if (out < 0) { close(in); return -1; }
    char buf[EJ_COPY_BUF];
    int rc = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; rc = -1; break; }
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) { if (errno == EINTR) continue; rc = -1; break; }
            off += w;
        }
        if (rc) break;
    }
    close(in);
    if (close(out) != 0) rc = -1;
    if (rc == 0) {
        struct timeval tv[2] = {{st->st_atime, 0}, {st->st_mtime, 0}};
        utimes(to, tv);
    } else {
        unlink(to);
    }
    return rc;
}

static int copy_tree(const char *from, const char *to, int depth) {
    struct stat st;
    if (depth > EJ_COPY_DEPTH || lstat(from, &st) != 0) return -1;
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        if (stat(from, &st) != 0 || S_ISDIR(st.st_mode)) return -1;
        return copy_file(from, to, &st);
    }
    if (mkdir(to, st.st_mode & 0777) != 0 && errno != EEXIST) return -1;
    DIR *d = opendir(from);
    if (!d) return -1;
    int rc = 0;
    struct dirent *ent;
    char src[1024], dst[1024];
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (snprintf(src, sizeof(src), "%s/%s", from, ent->d_name) >= (int)sizeof(src) ||
            snprintf(dst, sizeof(dst), "%s/%s", to, ent->d_name) >= (int)sizeof(dst)) {
            rc = -1;
            continue;
        }
        if (copy_tree(src, dst, depth + 1) != 0) rc = -1;
    }
    closedir(d);
    return rc;
}

/* Refuse to copy a folder into itself: /a -> /a/b would recurse forever. */
static int is_inside(const char *parent, const char *child) {
    size_t n = strlen(parent);
    while (n > 1 && parent[n - 1] == '/') n--;
    return strncmp(parent, child, n) == 0 && (child[n] == '/' || child[n] == '\0');
}

static int do_copy(const char *from, const char *to) {
    if (is_inside(from, to)) return -2;
    if (access(to, F_OK) == 0) return -3;
    return copy_tree(from, to, 0);
}

static int do_move(const char *from, const char *to) {
    if (is_inside(from, to)) return -2;
    if (access(to, F_OK) == 0) return -3;
    if (rename(from, to) == 0) return 0;
    if (errno != EXDEV) return -1;
    if (copy_tree(from, to, 0) != 0) { ej_rm_rf(to); return -1; }
    return ej_rm_rf(from);
}

typedef int (*pair_fn)(const char *, const char *);

static jint with_pair(JNIEnv *env, jstring jfrom, jstring jto, pair_fn fn) {
    const char *from = (*env)->GetStringUTFChars(env, jfrom, NULL);
    const char *to = (*env)->GetStringUTFChars(env, jto, NULL);
    int rc = -1;
    if (from && to) rc = fn(from, to);
    if (from) (*env)->ReleaseStringUTFChars(env, jfrom, from);
    if (to) (*env)->ReleaseStringUTFChars(env, jto, to);
    return rc;
}

/* 0 ok, -1 io error, -2 target inside source, -3 target exists. */
JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_copyPath(JNIEnv *env, jobject thiz, jstring jfrom, jstring jto) {
    (void)thiz;
    return with_pair(env, jfrom, jto, do_copy);
}

JNIEXPORT jint JNICALL
Java_org_agentej_android_Native_movePath(JNIEnv *env, jobject thiz, jstring jfrom, jstring jto) {
    (void)thiz;
    return with_pair(env, jfrom, jto, do_move);
}

#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char* execute_curl(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    size_t cap = 4096;
    size_t len = 0;
    char* buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }
    
    while (1) {
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        if (n == 0) break;
        len += n;
        if (len + 1 == cap) {
            cap *= 2;
            char* new_buf = realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = new_buf;
        }
    }
    buf[len] = '\0';
    pclose(fp);
    return buf;
}

char* http_post_json(const char* url, const char* auth_header, const char* json_body) {
    char temp_filename[1024];
    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = ".";
    snprintf(temp_filename, sizeof(temp_filename), "%s/agent-ej-XXXXXX", tmpdir);
    
    int fd = mkstemp(temp_filename);
    if (fd < 0) return NULL;
    
    size_t body_len = strlen(json_body);
    if (write(fd, json_body, body_len) != (ssize_t)body_len) {
        close(fd);
        unlink(temp_filename);
        return NULL;
    }
    close(fd);
    
    char cmd[4096];
    if (auth_header) {
        snprintf(cmd, sizeof(cmd), "curl -s --max-time 30 -X POST -H \"Content-Type: application/json\" -H \"%s\" -d @%s \"%s\"", auth_header, temp_filename, url);
    } else {
        snprintf(cmd, sizeof(cmd), "curl -s --max-time 30 -X POST -H \"Content-Type: application/json\" -d @%s \"%s\"", temp_filename, url);
    }
    
    char* res = execute_curl(cmd);
    unlink(temp_filename);
    return res;
}

char* http_get(const char* url, const char* auth_header) {
    char cmd[4096];
    if (auth_header) {
        snprintf(cmd, sizeof(cmd), "curl -s -H \"%s\" \"%s\"", auth_header, url);
    } else {
        snprintf(cmd, sizeof(cmd), "curl -s \"%s\"", url);
    }
    return execute_curl(cmd);
}

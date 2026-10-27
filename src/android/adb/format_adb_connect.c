#include "format_adb_internal.h"
#include "crypto.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    static uint32_t table[256];
    static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++)
                c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0);
            table[i] = c;
        }
        init = 1;
    }
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

int tcp_connect(const char *host, int port) {
    struct addrinfo hints = {0}, *ai;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    int ret = getaddrinfo(host, port_str, &hints, &ai);
    if (ret) return -1;
    int fd = -1;
    for (struct addrinfo *rp = ai; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv = {CONNECT_TIMEOUT, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(ai);
    return fd;
}

int read_msg(int fd, adb_msg *msg) {
    if (recv_all(fd, msg, sizeof(*msg))) return -1;
    return 0;
}

int write_msg(int fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
              const void *data, uint32_t len) {
    adb_msg msg = {0};
    msg.command = cmd;
    msg.arg0 = arg0;
    msg.arg1 = arg1;
    msg.data_length = len;
    msg.data_crc32 = data ? crc32(data, len) : 0;
    msg.magic = cmd ^ 0xFFFFFFFF;
    if (send_all(fd, &msg, sizeof(msg))) return -1;
    if (len && data && send_all(fd, data, len)) return -1;
    return 0;
}

int read_next_msg(int fd, adb_msg *msg, uint8_t **payload) {
    *payload = NULL;
    if (read_msg(fd, msg)) return -1;
    if (msg->data_length > 0) {
        if (msg->data_length > MAX_PAYLOAD) return -1;
        *payload = malloc(msg->data_length);
        if (!*payload || recv_all(fd, *payload, msg->data_length)) return -1;
    }
    return 0;
}

int adb_connect(adb_conn *conn) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = -1;

    // Try connecting directly to device on ADB port 5555 first
    {   const char *dev_host = getenv("ADB_DEVICE_HOST");
        if (!dev_host) dev_host = "192.168.240.112";
        conn->fd = tcp_connect(dev_host, 5555);
    }
    if (conn->fd < 0) {
        // Fallback: connect via ADB server at 127.0.0.1:5037
        conn->fd = tcp_connect("127.0.0.1", ADB_PORT);
        if (conn->fd < 0) return -1;

        const char *svc = "host:transport-any";
        int slen = strlen(svc);
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "%04x%s", slen, svc);
        if (send_all(conn->fd, cmd, 4 + slen)) { close(conn->fd); conn->fd = -1; return -1; }
        char ok[5] = {0};
        if (recv_all(conn->fd, ok, 4)) { close(conn->fd); conn->fd = -1; return -1; }
        if (ok[0] != 'O' || ok[1] != 'K') { close(conn->fd); conn->fd = -1; return -1; }
    }

    char system_type[64] = "agent-ej::";
    if (write_msg(conn->fd, A_CNXN, ADB_VERSION, MAX_PAYLOAD,
                  system_type, strlen(system_type) + 1))
        { close(conn->fd); conn->fd = -1; return -1; }

    rsa_key key;
    int key_loaded = 0;

    adb_msg msg;
    uint8_t *payload = NULL;

    int auth_retries = 0;
    while (auth_retries < 5) {
        if (read_next_msg(conn->fd, &msg, &payload)) {
            free(payload);
            close(conn->fd); conn->fd = -1; return -1;
        }

        if (msg.command == A_CNXN) {
            free(payload);
            return 0;
        }

        if (msg.command != A_AUTH) { free(payload); close(conn->fd); conn->fd = -1; return -1; }

        if (msg.arg0 == AUTH_TOKEN && payload && msg.data_length == 20) {
            if (!key_loaded) {
                char *home = getenv("HOME");
                char keypath[1024];
                snprintf(keypath, sizeof(keypath), "%s/.android/adbkey", home ? home : "");
                if (rsa_load_key(keypath, &key)) {
                    log_error("adb", "cannot load ADB key");
                    free(payload); close(conn->fd); conn->fd = -1; return -1;
                }
                key_loaded = 1;
            }

            uint8_t hash[20], sig[256];
            sha1(payload, 20, hash);
            rsa_sign(&key, hash, sig);

            if (write_msg(conn->fd, A_AUTH, AUTH_SIGNATURE, 0, sig, 256)) {
                free(payload); close(conn->fd); conn->fd = -1; return -1;
            }
            // Send public key
            char *home = getenv("HOME");
            char pubpath[1024];
            snprintf(pubpath, sizeof(pubpath), "%s/.android/adbkey.pub", home ? home : "");
            FILE *pfp = fopen(pubpath, "rb");
            if (!pfp) { free(payload); close(conn->fd); conn->fd = -1; return -1; }
            fseek(pfp, 0, SEEK_END);
            long pubsz = ftell(pfp);
            fseek(pfp, 0, SEEK_SET);
            char *pubkey = malloc(pubsz + 1);
            if (!pubkey) { fclose(pfp); free(payload); close(conn->fd); conn->fd = -1; return -1; }
            size_t nread = fread(pubkey, 1, pubsz, pfp);
            fclose(pfp);
            pubkey[nread] = '\0';
            // Trim trailing newline
            while (nread > 0 && (pubkey[nread-1] == '\n' || pubkey[nread-1] == '\r')) nread--;
            pubkey[nread] = '\0';
            write_msg(conn->fd, A_AUTH, AUTH_RSAPUBLICKEY, 0, pubkey, nread + 1);
            free(pubkey);
        }

        free(payload);
        payload = NULL;
    }

    return 0;
}

void adb_disconnect(adb_conn *conn) {
    if (conn->fd >= 0) {
        if (conn->local_id && conn->remote_id)
            write_msg(conn->fd, A_CLSE, conn->local_id, conn->remote_id, NULL, 0);
        close(conn->fd);
        conn->fd = -1;
    }
}

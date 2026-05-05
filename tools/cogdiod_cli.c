/*
 * cogdiod_cli.c — Command-line interface for the CogDiod bridge
 *
 * Usage:
 *   cogdiod-cli spawn <Type> <name> <strength> <confidence>
 *   cogdiod-cli get-tv <uuid>
 *   cogdiod-cli set-tv <uuid> <strength> <confidence>
 *   cogdiod-cli link <from_uuid> <to_uuid>
 *   cogdiod-cli unlink <from_uuid> <to_uuid>
 *   cogdiod-cli destroy <uuid>
 *   cogdiod-cli attend <uuid> <delta>
 *   cogdiod-cli get-sti <uuid>
 *   cogdiod-cli get-links <uuid>
 *   cogdiod-cli pln-deduce <ant_uuid> <impl_uuid>
 *   cogdiod-cli pln-revise <uuid> <strength2> <confidence2>
 *   cogdiod-cli episodic <uuid> <version>
 *   cogdiod-cli stats
 *   cogdiod-cli snapshot
 *
 * Connects to /tmp/cogdiod.sock (UNIX domain socket).
 *
 * Build:  cc -O2 -std=c11 -o cogdiod-cli tools/cogdiod_cli.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCK_PATH "/tmp/cogdiod.sock"
#define BUF_SIZE  65536

static int g_fd = -1;

static int bridge_connect(void) {
    g_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_fd < 0) { perror("socket"); return -1; }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
    if (connect(g_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return -1;
    }
    return 0;
}

static void bridge_call(const char* json) {
    char buf[BUF_SIZE];
    /* Send request + newline */
    size_t len = strlen(json);
    if (len + 2 > BUF_SIZE) { fprintf(stderr, "request too long\n"); return; }
    memcpy(buf, json, len);
    buf[len]   = '\n';
    buf[len+1] = '\0';
    send(g_fd, buf, len+1, 0);
    /* Read response */
    ssize_t n = recv(g_fd, buf, sizeof(buf)-1, 0);
    if (n <= 0) { fprintf(stderr, "no response\n"); return; }
    buf[n] = '\0';
    printf("%s", buf);
    if (buf[n-1] != '\n') printf("\n");
}

static void usage(void) {
    fprintf(stderr,
        "cogdiod-cli — CogDiod bridge command-line interface\n"
        "Connects to " SOCK_PATH "\n\n"
        "Commands:\n"
        "  spawn <Type> <name> <strength> <confidence>\n"
        "  get-tv <uuid>\n"
        "  set-tv <uuid> <strength> <confidence>\n"
        "  link <from> <to>\n"
        "  unlink <from> <to>\n"
        "  destroy <uuid>\n"
        "  attend <uuid> <delta>\n"
        "  get-sti <uuid>\n"
        "  get-links <uuid>\n"
        "  pln-deduce <ant_uuid> <impl_uuid>\n"
        "  pln-revise <uuid> <strength2> <confidence2>\n"
        "  episodic <uuid> <version>\n"
        "  stats\n"
        "  snapshot\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    if (bridge_connect() != 0) {
        fprintf(stderr, "Could not connect to bridge at %s\n"
                        "Is cogdiod_bridge running?\n", SOCK_PATH);
        return 1;
    }

    char json[BUF_SIZE];
    const char* cmd = argv[1];

    if (strcmp(cmd, "spawn") == 0) {
        if (argc < 6) { fprintf(stderr, "usage: spawn <Type> <name> <s> <c>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"spawn\",\"type\":\"%s\",\"name\":\"%s\","
            "\"strength\":%s,\"confidence\":%s}",
            argv[2], argv[3], argv[4], argv[5]);

    } else if (strcmp(cmd, "get-tv") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: get-tv <uuid>\n"); return 1; }
        snprintf(json, sizeof(json), "{\"op\":\"get_tv\",\"uuid\":%s}", argv[2]);

    } else if (strcmp(cmd, "set-tv") == 0) {
        if (argc < 5) { fprintf(stderr, "usage: set-tv <uuid> <s> <c>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"set_tv\",\"uuid\":%s,\"strength\":%s,\"confidence\":%s}",
            argv[2], argv[3], argv[4]);

    } else if (strcmp(cmd, "link") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: link <from> <to>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"link\",\"from\":%s,\"to\":%s}", argv[2], argv[3]);

    } else if (strcmp(cmd, "unlink") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: unlink <from> <to>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"unlink\",\"from\":%s,\"to\":%s}", argv[2], argv[3]);

    } else if (strcmp(cmd, "destroy") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: destroy <uuid>\n"); return 1; }
        snprintf(json, sizeof(json), "{\"op\":\"destroy\",\"uuid\":%s}", argv[2]);

    } else if (strcmp(cmd, "attend") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: attend <uuid> <delta>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"attend\",\"uuid\":%s,\"delta\":%s}", argv[2], argv[3]);

    } else if (strcmp(cmd, "get-sti") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: get-sti <uuid>\n"); return 1; }
        snprintf(json, sizeof(json), "{\"op\":\"get_sti\",\"uuid\":%s}", argv[2]);

    } else if (strcmp(cmd, "get-links") == 0) {
        if (argc < 3) { fprintf(stderr, "usage: get-links <uuid>\n"); return 1; }
        snprintf(json, sizeof(json), "{\"op\":\"get_links\",\"uuid\":%s}", argv[2]);

    } else if (strcmp(cmd, "pln-deduce") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: pln-deduce <ant_uuid> <impl_uuid>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"pln_deduce\",\"ant_uuid\":%s,\"impl_uuid\":%s}",
            argv[2], argv[3]);

    } else if (strcmp(cmd, "pln-revise") == 0) {
        if (argc < 5) { fprintf(stderr, "usage: pln-revise <uuid> <s2> <c2>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"pln_revise\",\"uuid\":%s,\"strength2\":%s,\"confidence2\":%s}",
            argv[2], argv[3], argv[4]);

    } else if (strcmp(cmd, "episodic") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: episodic <uuid> <version>\n"); return 1; }
        snprintf(json, sizeof(json),
            "{\"op\":\"episodic\",\"uuid\":%s,\"version\":%s}",
            argv[2], argv[3]);

    } else if (strcmp(cmd, "stats") == 0) {
        snprintf(json, sizeof(json), "{\"op\":\"stats\"}");

    } else if (strcmp(cmd, "snapshot") == 0) {
        snprintf(json, sizeof(json), "{\"op\":\"snapshot\"}");

    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage();
        close(g_fd);
        return 1;
    }

    bridge_call(json);
    close(g_fd);
    return 0;
}

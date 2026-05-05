/*
 * cogdiod_cli.c — CogDiod command-line client
 *
 * Connects to cogdiod_bridge on UNIX socket /tmp/cogdiod.sock
 * (or TCP port 19999 when given --tcp flag) and sends JSON requests
 * entered interactively or from a script.
 *
 * Usage:
 *   cogdiod_cli [--tcp [host[:port]]]
 *   echo '{"op":"stats"}' | cogdiod_cli
 *
 * Build:
 *   cc -O2 -std=c11 -o cogdiod_cli tools/cogdiod_cli.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define SOCK_PATH "/tmp/cogdiod.sock"
#define TCP_PORT  19999
#define BUF       65536

static int connect_unix(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect(unix)");
        close(fd);
        return -1;
    }
    return fd;
}

static int connect_tcp(const char* host, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (!host || strcmp(host, "localhost") == 0 || strcmp(host, "127.0.0.1") == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        inet_aton(host, &addr.sin_addr);
    }
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect(tcp)");
        close(fd);
        return -1;
    }
    return fd;
}

/* Send a JSON line and print the response */
static int do_request(int fd, const char* line) {
    /* Send */
    size_t n = strlen(line);
    char buf[BUF + 2];
    memcpy(buf, line, n);
    buf[n]   = '\n';
    buf[n+1] = '\0';
    if (send(fd, buf, n+1, 0) < 0) { perror("send"); return -1; }

    /* Receive line */
    size_t rn = 0;
    char resp[BUF+1];
    while (rn < BUF) {
        ssize_t got = recv(fd, resp + rn, 1, 0);
        if (got <= 0) break;
        if (resp[rn] == '\n') break;
        rn++;
    }
    resp[rn] = '\0';
    printf("%s\n", resp);
    return 0;
}

int main(int argc, char** argv) {
    int use_tcp = 0;
    const char* tcp_host = "127.0.0.1";
    uint16_t    tcp_port = TCP_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            use_tcp = 1;
            if (i+1 < argc && argv[i+1][0] != '-') {
                i++;
                /* May be host or host:port */
                char* colon = strchr(argv[i], ':');
                if (colon) {
                    *colon = '\0';
                    tcp_host = argv[i];
                    tcp_port = (uint16_t)atoi(colon+1);
                } else {
                    tcp_host = argv[i];
                }
            }
        }
    }

    int fd = use_tcp ? connect_tcp(tcp_host, tcp_port) : connect_unix();
    if (fd < 0) return 1;

    int interactive = isatty(STDIN_FILENO);
    char line[BUF];

    if (interactive) {
        fprintf(stderr, "cogdiod_cli connected (%s)\n",
                use_tcp ? "TCP" : "UNIX");
        fprintf(stderr, "Enter JSON requests, one per line. Ctrl-D to exit.\n");
    }

    while (1) {
        if (interactive) fprintf(stderr, "> ");
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        if (do_request(fd, line) < 0) break;
    }

    close(fd);
    return 0;
}

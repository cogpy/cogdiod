/*
 * cogdiod_bridge.c — Minimal CogDiod bridge server
 *
 * Listens on a UNIX-domain socket at /tmp/cogdiod.sock.
 * Speaks newline-delimited JSON.  Each language integration connects
 * as a client and uses this server as its AtomSpace.
 *
 * Build:  zig cc -O2 -std=c11 -o cogdiod_bridge cogdiod_bridge.c -lm
 * Run:    ./cogdiod_bridge &
 */

#define _GNU_SOURCE
#define BRIDGE_SOCK_PATH "/tmp/cogdiod.sock"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>

/* ── Atom store ─────────────────────────────────────────────────────────── */

#define MAX_ATOMS   4096
#define MAX_LINKS   16
#define NAME_LEN    64
#define TYPE_LEN    32
#define BUF_SIZE    65536
#define TV_HIST_MAX 8

typedef struct {
    uint64_t uuid;
    char     type[TYPE_LEN];
    char     name[NAME_LEN];
    float    strength;
    float    confidence;
    float    sti;
    float    lti;
    uint64_t out_links[MAX_LINKS];
    int      out_count;
    int      alive;
    /* episodic TV history */
    float    tv_hist_s[TV_HIST_MAX];
    float    tv_hist_c[TV_HIST_MAX];
    int      hist_count;
} Atom;

static Atom      atoms[MAX_ATOMS];
static int       atom_count = 0;
static uint64_t  next_uuid  = 1;
static pthread_mutex_t store_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static Atom* find_atom(uint64_t uuid) {
    for (int i = 0; i < atom_count; i++)
        if (atoms[i].alive && atoms[i].uuid == uuid) return &atoms[i];
    return NULL;
}

static Atom* alloc_atom(void) {
    if (atom_count >= MAX_ATOMS) return NULL;
    Atom* a = &atoms[atom_count++];
    memset(a, 0, sizeof(*a));
    a->alive      = 1;
    a->strength   = 0.5f;
    a->confidence = 0.1f;
    a->sti        = 0.0f;
    a->lti        = 0.0f;
    return a;
}

static void push_history(Atom* a) {
    if (a->hist_count < TV_HIST_MAX) {
        a->tv_hist_s[a->hist_count] = a->strength;
        a->tv_hist_c[a->hist_count] = a->confidence;
        a->hist_count++;
    } else {
        memmove(a->tv_hist_s, a->tv_hist_s + 1, (TV_HIST_MAX-1)*sizeof(float));
        memmove(a->tv_hist_c, a->tv_hist_c + 1, (TV_HIST_MAX-1)*sizeof(float));
        a->tv_hist_s[TV_HIST_MAX-1] = a->strength;
        a->tv_hist_c[TV_HIST_MAX-1] = a->confidence;
    }
}

/* ── JSON helpers (minimal, no external lib) ─────────────────────────────── */

static float json_float(const char* buf, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* p = strstr(buf, search);
    if (!p) return 0.0f;
    p += strlen(search);
    while (*p == ' ') p++;
    return (float)atof(p);
}

static uint64_t json_u64(const char* buf, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    return (uint64_t)strtoull(p, NULL, 10);
}

static void json_str(const char* buf, const char* key, char* out, size_t n) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* p = strstr(buf, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    while (*p == ' ' || *p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && i < n-1) out[i++] = *p++;
    out[i] = '\0';
}

static const char* json_op(const char* buf) {
    static char op[32];
    json_str(buf, "op", op, sizeof(op));
    return op;
}

/* ── Request handler ─────────────────────────────────────────────────────── */

static void handle(const char* req, char* resp, size_t resp_sz) {
    const char* op = json_op(req);
    pthread_mutex_lock(&store_lock);

    if (strcmp(op, "spawn") == 0) {
        Atom* a = alloc_atom();
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"full\"}"); goto done; }
        a->uuid = next_uuid++;
        json_str(req, "type", a->type, TYPE_LEN);
        json_str(req, "name", a->name, NAME_LEN);
        float s = json_float(req, "strength");
        float c = json_float(req, "confidence");
        if (s > 0) a->strength = s;
        if (c > 0) a->confidence = c;
        snprintf(resp, resp_sz, "{\"uuid\":%llu}", (unsigned long long)a->uuid);

    } else if (strcmp(op, "get_tv") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        snprintf(resp, resp_sz, "{\"strength\":%.4f,\"confidence\":%.4f}",
                 a->strength, a->confidence);

    } else if (strcmp(op, "set_tv") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        push_history(a);
        float s = json_float(req, "strength");
        float c = json_float(req, "confidence");
        if (s >= 0) a->strength = s;
        if (c >= 0) a->confidence = c;
        snprintf(resp, resp_sz, "{\"ok\":true}");

    } else if (strcmp(op, "link") == 0) {
        uint64_t from = json_u64(req, "from");
        uint64_t to   = json_u64(req, "to");
        Atom* a = find_atom(from);
        if (!a || a->out_count >= MAX_LINKS) {
            snprintf(resp, resp_sz, "{\"error\":\"link_failed\"}"); goto done;
        }
        a->out_links[a->out_count++] = to;
        snprintf(resp, resp_sz, "{\"ok\":true}");

    } else if (strcmp(op, "get_links") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"uuids\":[]}"); goto done; }
        char* p = resp;
        size_t used = (size_t)snprintf(p, resp_sz, "{\"uuids\":[");
        if (used < resp_sz) p += used;
        for (int i = 0; i < a->out_count; i++) {
            size_t rem = (p > resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
            if (rem == 0) break;
            if (i) { int w = snprintf(p, rem, ","); if (w > 0 && (size_t)w < rem) p += w; }
            rem = (p > resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
            if (rem > 0) {
                int w = snprintf(p, rem, "%llu", (unsigned long long)a->out_links[i]);
                if (w > 0 && (size_t)w < rem) p += w;
            }
        }
        { size_t rem = (p > resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
          if (rem > 0) snprintf(p, rem, "]}"); }
    } else if (strcmp(op, "get_sti") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        snprintf(resp, resp_sz, "{\"sti\":%.4f}", a->sti);

    } else if (strcmp(op, "attend") == 0) {
        uint64_t uuid  = json_u64(req, "uuid");
        float    delta = json_float(req, "delta");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        a->sti += delta;
        snprintf(resp, resp_sz, "{\"ok\":true,\"sti\":%.4f}", a->sti);

    } else if (strcmp(op, "snapshot") == 0) {
        char* p = resp;
        size_t used = (size_t)snprintf(p, resp_sz, "{\"atoms\":[");
        if (used < resp_sz) p += used;
        int first = 1;
        for (int i = 0; i < atom_count; i++) {
            if (!atoms[i].alive) continue;
            size_t rem = (p >= resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
            if (rem < 4) break;
            if (!first) {
                int w = snprintf(p, rem, ",");
                if (w > 0 && (size_t)w < rem) p += w;
            }
            first = 0;
            rem = (p >= resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
            if (rem > 0) {
                int w = snprintf(p, rem,
                    "{\"uuid\":%llu,\"type\":\"%s\",\"name\":\"%s\","
                    "\"strength\":%.4f,\"confidence\":%.4f,\"sti\":%.4f}",
                    (unsigned long long)atoms[i].uuid,
                    atoms[i].type, atoms[i].name,
                    atoms[i].strength, atoms[i].confidence, atoms[i].sti);
                if (w > 0 && (size_t)w < rem) p += w;
            }
        }
        { size_t rem = (p >= resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
          if (rem > 0) snprintf(p, rem, "]}"); }

    } else if (strcmp(op, "stats") == 0) {
        float total_sti = 0;
        for (int i = 0; i < atom_count; i++)
            if (atoms[i].alive) total_sti += atoms[i].sti;
        snprintf(resp, resp_sz,
                 "{\"atom_count\":%d,\"total_sti\":%.4f}",
                 atom_count, total_sti);

    } else if (strcmp(op, "rewrite_rule") == 0) {
        /*
         * Simplified rewrite: find all atoms whose type matches
         * "from_pattern" and set their TV to the "to_pattern" TV.
         * A real Maude-style rewriter would match term structure.
         */
        char from_type[TYPE_LEN], to_type[TYPE_LEN];
        json_str(req, "from_type", from_type, TYPE_LEN);
        json_str(req, "to_type",   to_type,   TYPE_LEN);
        float new_s = json_float(req, "new_strength");
        float new_c = json_float(req, "new_confidence");
        int applied = 0;
        for (int i = 0; i < atom_count; i++) {
            if (!atoms[i].alive) continue;
            if (strcmp(atoms[i].type, from_type) == 0) {
                if (new_s > 0) atoms[i].strength   = new_s;
                if (new_c > 0) atoms[i].confidence = new_c;
                applied++;
            }
        }
        snprintf(resp, resp_sz, "{\"ok\":true,\"applied\":%d}", applied);

    } else if (strcmp(op, "unlink") == 0) {
        uint64_t from = json_u64(req, "from");
        uint64_t to   = json_u64(req, "to");
        Atom* a = find_atom(from);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        int removed = 0;
        for (int i = 0; i < a->out_count; i++) {
            if (a->out_links[i] == to) {
                memmove(&a->out_links[i], &a->out_links[i+1],
                        (a->out_count - i - 1) * sizeof(uint64_t));
                a->out_count--;
                removed = 1;
                break;
            }
        }
        snprintf(resp, resp_sz, "{\"ok\":%s}", removed ? "true" : "false");

    } else if (strcmp(op, "destroy") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        a->alive = 0;
        snprintf(resp, resp_sz, "{\"ok\":true}");

    } else if (strcmp(op, "pln_deduce") == 0) {
        /* P(C|A) given P(B|A) and P(C|B) via modus ponens */
        uint64_t a_uuid    = json_u64(req, "antecedent");
        uint64_t impl_uuid = json_u64(req, "implication");
        Atom* ant  = find_atom(a_uuid);
        Atom* impl = find_atom(impl_uuid);
        if (!ant || !impl) {
            snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done;
        }
        float s = ant->strength * impl->strength;
        float c = ant->confidence * impl->confidence * 0.9f;
        snprintf(resp, resp_sz,
                 "{\"strength\":%.4f,\"confidence\":%.4f}", s, c);

    } else if (strcmp(op, "pln_revise") == 0) {
        /* Bayesian revision of two truth-value estimates */
        float s1 = json_float(req, "s1"), c1 = json_float(req, "c1");
        float s2 = json_float(req, "s2"), c2 = json_float(req, "c2");
        float k  = 1.0f;
        float cn = c1 + c2;
        float sn = (cn > 0) ? (s1*c1 + s2*c2) / cn : 0.0f;
        float cc = cn / (cn + k);
        snprintf(resp, resp_sz,
                 "{\"strength\":%.4f,\"confidence\":%.4f}", sn, cc);

    } else if (strcmp(op, "episodic") == 0) {
        uint64_t uuid = json_u64(req, "uuid");
        Atom* a = find_atom(uuid);
        if (!a) { snprintf(resp, resp_sz, "{\"error\":\"not_found\"}"); goto done; }
        char* p = resp;
        size_t used = (size_t)snprintf(p, resp_sz, "{\"history\":[");
        if (used < resp_sz) p += used;
        for (int i = 0; i < a->hist_count; i++) {
            size_t rem = (p >= resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
            if (rem < 4) break;
            if (i) { int w = snprintf(p, rem, ","); if (w > 0 && (size_t)w < rem) p += w; }
            rem = (p >= resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
            if (rem > 0) {
                int w = snprintf(p, rem, "{\"s\":%.4f,\"c\":%.4f}",
                                 a->tv_hist_s[i], a->tv_hist_c[i]);
                if (w > 0 && (size_t)w < rem) p += w;
            }
        }
        { size_t rem = (p >= resp + resp_sz) ? 0 : (size_t)(resp + resp_sz - p);
          if (rem > 0) snprintf(p, rem, "]}"); }

    } else {
        snprintf(resp, resp_sz, "{\"error\":\"unknown_op\",\"op\":\"%s\"}", op);
    }

done:
    pthread_mutex_unlock(&store_lock);
}

/* ── Client thread ───────────────────────────────────────────────────────── */

static void* client_thread(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    char req[BUF_SIZE], resp[BUF_SIZE];
    ssize_t n;
    while ((n = recv(fd, req, sizeof(req)-1, 0)) > 0) {
        req[n] = '\0';
        /* Strip trailing newline */
        char* nl = strchr(req, '\n');
        if (nl) *nl = '\0';
        if (strlen(req) == 0) continue;
        memset(resp, 0, sizeof(resp));
        handle(req, resp, sizeof(resp));
        /* Append newline delimiter */
        size_t rlen = strlen(resp);
        resp[rlen]   = '\n';
        resp[rlen+1] = '\0';
        send(fd, resp, rlen+1, 0);
    }
    close(fd);
    return NULL;
}

/* ── TCP listener thread (port 19999) ────────────────────────────────────── */

static void* tcp_accept_thread(void* arg) {
    int srv = *(int*)arg;
    free(arg);
    for (;;) {
        int* cfd = malloc(sizeof(int));
        *cfd = accept(srv, NULL, NULL);
        if (*cfd < 0) { free(cfd); break; }
        pthread_t t;
        pthread_create(&t, NULL, client_thread, cfd);
        pthread_detach(t);
    }
    return NULL;
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void) {
    unlink(BRIDGE_SOCK_PATH);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BRIDGE_SOCK_PATH, sizeof(addr.sun_path)-1);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(srv, 16);
    fprintf(stderr, "[cogdiod_bridge] UNIX socket: %s\n", BRIDGE_SOCK_PATH);

    /* Also listen on TCP port 19999 */
    int tcp_srv = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_srv >= 0) {
        int opt = 1;
        setsockopt(tcp_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in taddr = {0};
        taddr.sin_family      = AF_INET;
        taddr.sin_port        = htons(19999);
        taddr.sin_addr.s_addr = INADDR_ANY;
        if (bind(tcp_srv, (struct sockaddr*)&taddr, sizeof(taddr)) == 0) {
            listen(tcp_srv, 16);
            fprintf(stderr, "[cogdiod_bridge] TCP port 19999\n");
            int* sp = malloc(sizeof(int));
            *sp = tcp_srv;
            pthread_t tt;
            pthread_create(&tt, NULL, tcp_accept_thread, sp);
            pthread_detach(tt);
        }
    }

    for (;;) {
        int* cfd = malloc(sizeof(int));
        *cfd = accept(srv, NULL, NULL);
        if (*cfd < 0) { free(cfd); continue; }
        pthread_t t;
        pthread_create(&t, NULL, client_thread, cfd);
        pthread_detach(t);
    }
}
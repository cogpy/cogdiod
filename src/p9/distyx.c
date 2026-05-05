/*
 * distyx.c — DisTyx: the cognitive 9P/Styx protocol layer for CogDiod
 *
 * DisTyx extends 9P2000.L with five cognitive operations:
 *
 *   SPAWN   — Tlcreate on /ai/atoms/<type>/<name>  → new AtomIsolate
 *   DESTROY — Tunlinkat                             → GC the isolate
 *   READ_TV — Tread on /ai/atoms/<uuid>/tv          → TruthValue bytes
 *   WRITE_TV— Twrite on /ai/atoms/<uuid>/tv         → update TruthValue
 *   LINK    — Tlink  /ai/atoms/<src>/out/<dst>      → LimboChannel
 *   ATTEND  — Twrite on /ai/atoms/<uuid>/sti        → STI delta
 *   INFER   — Twrite on /ai/atoms/<uuid>/infer      → trigger inference
 *
 * The namespace layout served by DisTyx:
 *
 *   /ai/
 *     atoms/
 *       <type>/               ← directory of all atoms of this type
 *         <name_or_uuid>/
 *           tv                ← TruthValue (8 bytes: 2x float32)
 *           av                ← AttentionValue (8 bytes: 2x float32)
 *           ctl               ← control file: "infer\n", "gc\n"
 *           in/               ← incoming channels (read-only listing)
 *           out/              ← outgoing channels (write to link)
 *     packages/
 *       <type>.elm            ← package bytecode (read to fetch)
 *     stats                   ← kernel statistics (JSON)
 */

#include "cogdiod.h"
#include "distyx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ─────────────────────────────────────────────────────────────────────────
 * 9P message types (subset of 9P2000.L used by DisTyx)
 * ───────────────────────────────────────────────────────────────────────── */

#define P9_TVERSION  100
#define P9_RVERSION  101
#define P9_TATTACH   104
#define P9_RATTACH   105
#define P9_TERROR    106
#define P9_RERROR    107
#define P9_TWALK     110
#define P9_RWALK     111
#define P9_TLOPEN    112
#define P9_RLOPEN    113
#define P9_TLCREATE  114
#define P9_RLCREATE  115
#define P9_TREAD     116
#define P9_RREAD     117
#define P9_TWRITE    118
#define P9_RWRITE    119
#define P9_TCLUNK    120
#define P9_RCLUNK    121
#define P9_TREMOVE   122
#define P9_RREMOVE   123
#define P9_TGETATTR  124
#define P9_RGETATTR  125
#define P9_TSETATTR  126
#define P9_RSETATTR  127
#define P9_TLINK     170
#define P9_RLINK     171

/* DisTyx cognitive extensions (above standard 9P range) */
#define DT_TINFER    200
#define DT_RINFER    201
#define DT_TATTEND   202
#define DT_RATTEND   203
#define DT_TSPAWN    204
#define DT_RSPAWN    205

/* ─────────────────────────────────────────────────────────────────────────
 * Minimal 9P wire format helpers
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t size;
    uint8_t  type;
    uint16_t tag;
    uint8_t  data[DISTYX_MSIZE];
} P9Msg;

static void p9_put_u16(uint8_t* buf, uint16_t v) {
    buf[0] = v & 0xFF; buf[1] = (v >> 8) & 0xFF;
}
static void p9_put_u32(uint8_t* buf, uint32_t v) {
    buf[0] = v & 0xFF; buf[1] = (v >> 8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF; buf[3] = (v >> 24) & 0xFF;
}
static uint32_t p9_get_u32(const uint8_t* buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}
static uint16_t p9_get_u16(const uint8_t* buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* ─────────────────────────────────────────────────────────────────────────
 * DisTyx path parser
 *
 * Parses /ai/atoms/<type>/<name>/<file> into its components.
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    char segment[8][64];   /* path segments */
    int  depth;
} DisTyxPath;

static int distyx_parse_path(const char* path, DisTyxPath* p) {
    p->depth = 0;
    char tmp[512];
    strncpy(tmp, path, 511);
    char* tok = strtok(tmp, "/");
    while (tok && p->depth < 8) {
        strncpy(p->segment[p->depth++], tok, 63);
        tok = strtok(NULL, "/");
    }
    return p->depth;
}

/* ─────────────────────────────────────────────────────────────────────────
 * DisTyx request handlers
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * Handle a Tlcreate on /ai/atoms/<type>/<name>
 * → spawns a new AtomIsolate and returns its UUID as the Qid.
 */
static int distyx_handle_create(CogDiodKernel* k,
                                const char* type_name,
                                const char* atom_name,
                                uint64_t* out_uuid) {
    AtomIsolate* a = cogdiod_spawn(k, type_name, atom_name);
    if (!a) return -1;
    *out_uuid = a->uuid;
    return 0;
}

/*
 * Handle a Tread on /ai/atoms/<uuid>/tv
 * → returns 8 bytes: [strength:f32][confidence:f32]
 */
static int distyx_handle_read_tv(CogDiodKernel* k,
                                 uint64_t uuid,
                                 uint8_t* out_buf, size_t* out_len) {
    TruthValue tv = cogdiod_get_tv(k, uuid);
    memcpy(out_buf,     &tv.strength,   4);
    memcpy(out_buf + 4, &tv.confidence, 4);
    *out_len = 8;
    return 0;
}

/*
 * Handle a Twrite on /ai/atoms/<uuid>/tv
 * → parses 8 bytes and updates the TruthValue.
 */
static int distyx_handle_write_tv(CogDiodKernel* k,
                                  uint64_t uuid,
                                  const uint8_t* buf, size_t len) {
    if (len < 8) return -1;
    TruthValue tv;
    memcpy(&tv.strength,   buf,     4);
    memcpy(&tv.confidence, buf + 4, 4);
    return cogdiod_set_tv(k, uuid, tv);
}

/*
 * Handle a Twrite on /ai/atoms/<uuid>/sti
 * → parses 4 bytes (float32) and calls cogdiod_attend.
 */
static int distyx_handle_attend(CogDiodKernel* k,
                                uint64_t uuid,
                                const uint8_t* buf, size_t len) {
    if (len < 4) return -1;
    float delta;
    memcpy(&delta, buf, 4);
    return cogdiod_attend(k, uuid, delta);
}

/*
 * Handle a Tlink on /ai/atoms/<src_uuid>/out/<dst_uuid>
 * → creates a LimboChannel between the two atoms.
 */
static int distyx_handle_link(CogDiodKernel* k,
                              uint64_t src_uuid, uint64_t dst_uuid) {
    LimboChannel* ch = cogdiod_link(k, src_uuid, dst_uuid);
    return ch ? 0 : -1;
}

/*
 * Handle a Twrite on /ai/atoms/<uuid>/ctl with "infer\n"
 * → sends MSG_INFER to all outgoing channels of the atom.
 */
static int distyx_handle_infer(CogDiodKernel* k, uint64_t uuid) {
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    if (!a) return -1;

    CogMessage msg = {
        .type        = MSG_INFER,
        .sender_uuid = uuid,
    };
    LimboChannel* ch = a->outgoing;
    while (ch) {
        cogdiod_send(ch, &msg);
        ch = ch->out_next;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * DisTyx stats serialiser
 * ───────────────────────────────────────────────────────────────────────── */

static int distyx_stats(CogDiodKernel* k, char* buf, size_t max) {
    return snprintf(buf, max,
        "{"
        "\"atoms\":%llu,"
        "\"packages\":%u,"
        "\"total_sti\":%.2f,"
        "\"sti_funds\":%.2f,"
        "\"workers\":%u"
        "}\n",
        (unsigned long long)k->atom_count,
        k->pkg_count,
        k->total_sti,
        k->sti_funds,
        k->worker_count);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Top-level DisTyx request dispatcher
 *
 * In a full implementation this would be driven by the diod libnpfs
 * callback table.  Here we expose a clean C dispatch function that
 * maps a parsed path + operation to the appropriate handler.
 * ───────────────────────────────────────────────────────────────────────── */




int distyx_dispatch(CogDiodKernel* k,
                    const DisTyxRequest* req,
                    DisTyxResponse* resp) {
    DisTyxPath p;
    distyx_parse_path(req->path, &p);
    resp->buf_len = 0;
    resp->status  = 0;

    /* /ai/stats */
    if (p.depth == 2 && strcmp(p.segment[1], "stats") == 0) {
        resp->buf_len = distyx_stats(k, (char*)resp->buf,
                                     sizeof(resp->buf));
        return 0;
    }

    /* /ai/atoms/... */
    if (p.depth >= 2 && strcmp(p.segment[1], "atoms") == 0) {

        /* CREATE: /ai/atoms/<type>/<name>  (Tlcreate) */
        if (req->op == DT_OP_CREATE && p.depth == 4) {
            uint64_t uuid = 0;
            if (distyx_handle_create(k, p.segment[2], p.segment[3],
                                     &uuid) != 0) {
                snprintf(resp->errmsg, 127, "spawn failed for type '%s'",
                         p.segment[2]);
                resp->status = -1;
                return -1;
            }
            /* Return UUID as 8-byte little-endian */
            memcpy(resp->buf, &uuid, 8);
            resp->buf_len = 8;
            return 0;
        }

        /* READ /ai/atoms/<uuid>/tv */
        if (req->op == DT_OP_READ && p.depth == 4
            && strcmp(p.segment[3], "tv") == 0) {
            uint64_t uuid = strtoull(p.segment[2], NULL, 10);
            return distyx_handle_read_tv(k, uuid,
                                         resp->buf, &resp->buf_len);
        }

        /* WRITE /ai/atoms/<uuid>/tv */
        if (req->op == DT_OP_WRITE && p.depth == 4
            && strcmp(p.segment[3], "tv") == 0) {
            uint64_t uuid = strtoull(p.segment[2], NULL, 10);
            return distyx_handle_write_tv(k, uuid,
                                          req->buf, req->buf_len);
        }

        /* WRITE /ai/atoms/<uuid>/sti */
        if (req->op == DT_OP_WRITE && p.depth == 4
            && strcmp(p.segment[3], "sti") == 0) {
            uint64_t uuid = strtoull(p.segment[2], NULL, 10);
            return distyx_handle_attend(k, uuid,
                                        req->buf, req->buf_len);
        }

        /* WRITE /ai/atoms/<uuid>/ctl */
        if (req->op == DT_OP_WRITE && p.depth == 4
            && strcmp(p.segment[3], "ctl") == 0) {
            uint64_t uuid = strtoull(p.segment[2], NULL, 10);
            if (strncmp((char*)req->buf, "infer", 5) == 0)
                return distyx_handle_infer(k, uuid);
        }

        /* LINK /ai/atoms/<src>/out/<dst> */
        if (req->op == DT_OP_LINK && p.depth == 5
            && strcmp(p.segment[3], "out") == 0) {
            uint64_t src = strtoull(p.segment[2], NULL, 10);
            uint64_t dst = strtoull(p.segment[4], NULL, 10);
            return distyx_handle_link(k, src, dst);
        }

        /* REMOVE /ai/atoms/<uuid> */
        if (req->op == DT_OP_REMOVE && p.depth == 3) {
            uint64_t uuid = strtoull(p.segment[2], NULL, 10);
            return cogdiod_destroy_atom(k, uuid);
        }
    }

    snprintf(resp->errmsg, 127, "unknown path: %s", req->path);
    resp->status = -1;
    return -1;
}

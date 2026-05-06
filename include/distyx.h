/*
 * distyx.h — DisTyx 9P/Styx cognitive protocol layer — public types
 */

#pragma once

#include "cogdiod.h"

/* ─────────────────────────────────────────────────────────────────────────
 * DisTyx operation codes
 * ───────────────────────────────────────────────────────────────────────── */

typedef enum {
    DT_OP_CREATE,
    DT_OP_READ,
    DT_OP_WRITE,
    DT_OP_LINK,
    DT_OP_REMOVE,
    DT_OP_STAT,
} DisTyxOp;

/* ─────────────────────────────────────────────────────────────────────────
 * DisTyx request / response
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    DisTyxOp   op;
    char       path[512];
    uint64_t   fid;
    uint8_t    buf[DISTYX_MSIZE];
    size_t     buf_len;
} DisTyxRequest;

typedef struct {
    int        status;
    uint8_t    buf[DISTYX_MSIZE];
    size_t     buf_len;
    char       errmsg[128];
} DisTyxResponse;

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

int distyx_dispatch(CogDiodKernel* k,
                    const DisTyxRequest* req,
                    DisTyxResponse* resp);

int distyx_start_tcp(CogDiodKernel* k, uint16_t port);

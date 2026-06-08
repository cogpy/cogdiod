/*
 * time_node_pkg.c — C stub package for TimeNode (Phase 2.13)
 *
 * TimeNode stores a specific timestamp value. It uses the numeric value
 * storage (similar to NumberNode) but with temporal semantics.
 * The timestamp is stored in registers; LTI encodes temporal decay.
 *
 * Behaviour:
 *   init:       Initialize with timestamp (stored in heap/registers)
 *   on-message: MSG_QUERY returns timestamp; MSG_SOURCE_CHANGED updates relevance
 *   on-gc:      Cleanup
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t time_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    OP_HALT
};

/*
 * on-message:
 *   - MSG_QUERY: Return timestamp value
 *   - MSG_SOURCE_CHANGED: No-op (timestamps are immutable)
 */
static const uint8_t time_msg_bc[] = {
    OP_GET_TV,
    /* Return timestamp value in response */
    OP_HALT
};

static const uint8_t time_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* time_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "TimeNode",
        .init_bc     = time_init_bc,
        .init_bc_len = sizeof(time_init_bc),
        .msg_bc      = time_msg_bc,
        .msg_bc_len  = sizeof(time_msg_bc),
        .gc_bc       = time_gc_bc,
        .gc_bc_len   = sizeof(time_gc_bc),
    };
    return elm_build_stub(&def);
}

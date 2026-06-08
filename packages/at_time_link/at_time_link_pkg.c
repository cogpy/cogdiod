/*
 * at_time_link_pkg.c — C stub package for AtTimeLink (Phase 2.13)
 *
 * AtTimeLink represents an event at a specific timestamp.
 * Structure: AtTimeLink(Event, TimeNode)
 * The timestamp is stored in the TimeNode; LTI encodes temporal decay.
 *
 * Behaviour:
 *   init:       Initialize with current timestamp
 *   on-message: MSG_QUERY returns time-stamped event; MSG_SOURCE_CHANGED propagates
 *   on-gc:      Cleanup temporal data
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t at_time_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    /* Set LTI based on timestamp recency */
    OP_GET_STI,
    OP_HALT
};

/*
 * on-message:
 *   - MSG_QUERY: Return the event with its timestamp
 *   - MSG_SOURCE_CHANGED: Update temporal relevance
 */
static const uint8_t at_time_msg_bc[] = {
    OP_GET_TV,
    /* Temporal decay based on distance from current time */
    OP_PLN_REV,      /* Revise with temporal decay */
    OP_SET_TV,
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t at_time_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* at_time_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "AtTimeLink",
        .init_bc     = at_time_init_bc,
        .init_bc_len = sizeof(at_time_init_bc),
        .msg_bc      = at_time_msg_bc,
        .msg_bc_len  = sizeof(at_time_msg_bc),
        .gc_bc       = at_time_gc_bc,
        .gc_bc_len   = sizeof(at_time_gc_bc),
    };
    return elm_build_stub(&def);
}

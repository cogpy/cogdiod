/*
 * execution_link_pkg.c — C stub package for ExecutionLink (Phase 2.10)
 *
 * ExecutionLink triggers GroundedSchemaNode.execute() on MSG_INFER.
 * Structure: ExecutionLink(GroundedSchemaNode, ListLink of arguments)
 *
 * Behaviour:
 *   init:       Initialize execution context
 *   on-message: MSG_INFER triggers schema execution; MSG_SOURCE_CHANGED propagates
 *   on-gc:      Cleanup any execution context
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t execution_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    OP_HALT
};

/*
 * on-message:
 *   - On MSG_INFER (0x03): Send MSG_INFER to the GroundedSchemaNode (first outgoing)
 *   - On MSG_SOURCE_CHANGED: Re-spread attention and propagate
 */
static const uint8_t execution_msg_bc[] = {
    /* Check message type and trigger execution */
    OP_GET_TV,
    OP_SEND,         /* Send to schema node to trigger execution */
    OP_ECAN_SP,      /* Spread attention */
    OP_HALT
};

static const uint8_t execution_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* execution_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "ExecutionLink",
        .init_bc     = execution_init_bc,
        .init_bc_len = sizeof(execution_init_bc),
        .msg_bc      = execution_msg_bc,
        .msg_bc_len  = sizeof(execution_msg_bc),
        .gc_bc       = execution_gc_bc,
        .gc_bc_len   = sizeof(execution_gc_bc),
    };
    return elm_build_stub(&def);
}

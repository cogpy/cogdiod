/*
 * context_link_pkg.c — C stub package for ContextLink (Phase 2.11)
 *
 * ContextLink provides scoped evaluation context. It wraps a child link
 * with a context atom that defines the evaluation environment.
 * Structure: ContextLink(ContextAtom, ChildLink)
 *
 * Behaviour:
 *   init:       Initialize context bindings
 *   on-message: MSG_INFER evaluates child in context; MSG_SOURCE_CHANGED propagates
 *   on-gc:      Cleanup context
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t context_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    OP_HALT
};

/*
 * on-message:
 *   - MSG_INFER: Evaluate the child link in the context defined by context atom
 *   - MSG_SOURCE_CHANGED: Propagate changes with context-modified TV
 */
static const uint8_t context_msg_bc[] = {
    OP_GET_TV,
    /* Get context TV from first outgoing (context atom) */
    /* Combine with child TV from second outgoing */
    OP_PLN_REV,      /* Context-weighted revision */
    OP_SET_TV,
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t context_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* context_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "ContextLink",
        .init_bc     = context_init_bc,
        .init_bc_len = sizeof(context_init_bc),
        .msg_bc      = context_msg_bc,
        .msg_bc_len  = sizeof(context_msg_bc),
        .gc_bc       = context_gc_bc,
        .gc_bc_len   = sizeof(context_gc_bc),
    };
    return elm_build_stub(&def);
}

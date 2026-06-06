/*
 * list_link_pkg.c — C stub package for ListLink
 *
 * ListLink behaviour (Phase 2.4):
 *   - Ordered list of child atom UUIDs
 *   - MSG_SOURCE_CHANGED rebroadcasts to all children
 *   - Maintains ordered sequence semantics
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t list_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: propagate changes to all children
 *   ListLink acts as an aggregator, spreading attention to its ordered members
 */
static const uint8_t list_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = list TV (metadata) */
    OP_ECAN_SP,     /* spread to all children in order */
    OP_HALT
};

static const uint8_t list_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* list_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "ListLink",
        .init_bc     = list_init_bc,
        .init_bc_len = sizeof(list_init_bc),
        .msg_bc      = list_msg_bc,
        .msg_bc_len  = sizeof(list_msg_bc),
        .gc_bc       = list_gc_bc,
        .gc_bc_len   = sizeof(list_gc_bc),
    };
    return elm_build_stub(&def);
}

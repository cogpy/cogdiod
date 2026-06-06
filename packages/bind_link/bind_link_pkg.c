/*
 * bind_link_pkg.c — C stub package for BindLink
 *
 * BindLink behaviour (Phase 2.14, Phase 3):
 *   - Pattern match root for query execution
 *   - MSG_QUERY triggers pattern matching engine
 *   - Returns bindings satisfying the pattern
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t bind_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: pattern match execution
 *   MSG_QUERY: run pattern match and send results
 *   Results are sent as MSG_CUSTOM with binding data
 */
static const uint8_t bind_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = pattern confidence */
    /* Pattern matching would be done by elm_exec_msg calling into C */
    OP_ECAN_SP,     /* spread results */
    OP_HALT
};

static const uint8_t bind_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* bind_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "BindLink",
        .init_bc     = bind_init_bc,
        .init_bc_len = sizeof(bind_init_bc),
        .msg_bc      = bind_msg_bc,
        .msg_bc_len  = sizeof(bind_msg_bc),
        .gc_bc       = bind_gc_bc,
        .gc_bc_len   = sizeof(bind_gc_bc),
    };
    return elm_build_stub(&def);
}

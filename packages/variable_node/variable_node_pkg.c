/*
 * variable_node_pkg.c — C stub package for VariableNode
 *
 * VariableNode behaviour (Phase 2.3):
 *   - Placeholder in pattern templates
 *   - Responds to MSG_QUERY with its binding or UNBOUND
 *   - Used by pattern matching engine (Phase 3)
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t var_init_bc[] = {
    OP_NOP,         /* Variables start unbound */
    OP_HALT
};

/*
 * on-message:
 *   MSG_QUERY: return binding status in response
 *   MSG_UPDATE_TV: bind variable to value (via TV encoding)
 */
static const uint8_t var_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = binding status (TV encodes bound state) */
    OP_ECAN_SP,     /* Notify query source */
    OP_HALT
};

static const uint8_t var_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* variable_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "VariableNode",
        .init_bc     = var_init_bc,
        .init_bc_len = sizeof(var_init_bc),
        .msg_bc      = var_msg_bc,
        .msg_bc_len  = sizeof(var_msg_bc),
        .gc_bc       = var_gc_bc,
        .gc_bc_len   = sizeof(var_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * predicate_node_pkg.c — C stub package for PredicateNode
 *
 * PredicateNode behaviour (Phase 2.1):
 *   - Holds a predicate name and TV
 *   - Responds to MSG_INFER by evaluating grounded predicates
 *   - MSG_UPDATE_TV revises TV using PLN_REV
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t pred_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: handles MSG_INFER and MSG_UPDATE_TV
 *   MSG_INFER: spread attention downstream (predicate evaluation)
 *   MSG_UPDATE_TV: revise TV using PLN_REV
 */
static const uint8_t pred_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = current TV */
    OP_PLN_REV,     /* revise(current, incoming) → regs[4..5] */
    OP_SET_TV,      /* store revised TV */
    OP_ECAN_SP,     /* spread attention */
    OP_HALT
};

static const uint8_t pred_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* predicate_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "PredicateNode",
        .init_bc     = pred_init_bc,
        .init_bc_len = sizeof(pred_init_bc),
        .msg_bc      = pred_msg_bc,
        .msg_bc_len  = sizeof(pred_msg_bc),
        .gc_bc       = pred_gc_bc,
        .gc_bc_len   = sizeof(pred_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * equivalence_link_pkg.c — C stub package for EquivalenceLink
 *
 * EquivalenceLink behaviour (Phase 2.8):
 *   - Bidirectional implication (A ⟺ B)
 *   - Merges TV with PLN_REV in both directions
 *   - Symmetric: A⟺B has same TV as B⟺A
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t equiv_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: bidirectional PLN revision
 *   Evidence from either direction revises the equivalence TV
 */
static const uint8_t equiv_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = equivalence TV */
    OP_PLN_REV,     /* revise with incoming evidence */
    OP_SET_TV,      /* update equivalence */
    OP_ECAN_SP,     /* spread to both endpoints */
    OP_HALT
};

static const uint8_t equiv_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* equivalence_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "EquivalenceLink",
        .init_bc     = equiv_init_bc,
        .init_bc_len = sizeof(equiv_init_bc),
        .msg_bc      = equiv_msg_bc,
        .msg_bc_len  = sizeof(equiv_msg_bc),
        .gc_bc       = equiv_gc_bc,
        .gc_bc_len   = sizeof(equiv_gc_bc),
    };
    return elm_build_stub(&def);
}

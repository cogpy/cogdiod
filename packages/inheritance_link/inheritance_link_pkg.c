/*
 * inheritance_link_pkg.c — C stub package for InheritanceLink
 *
 * InheritanceLink behaviour (Phase 2.5):
 *   - Subtype/supertype relation between ConceptNodes
 *   - Triggers PLN_DED when either end changes TV
 *   - Supports transitive inheritance reasoning
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t inh_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: PLN deduction for inheritance
 *   When subtype TV changes, deduce supertype TV
 *   regs[0..1] = this link's TV (inheritance strength)
 *   regs[10..11] = subtype TV (from MSG_SOURCE_CHANGED)
 *   PLN_DED → supertype TV
 */
static const uint8_t inh_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = P(Super|Sub) */
    OP_PLN_DED,     /* deduce(P(Super|Sub), P(Sub)) → regs[4..5] */
    OP_ECAN_SP,     /* Spread to supertype */
    OP_HALT
};

static const uint8_t inh_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* inheritance_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "InheritanceLink",
        .init_bc     = inh_init_bc,
        .init_bc_len = sizeof(inh_init_bc),
        .msg_bc      = inh_msg_bc,
        .msg_bc_len  = sizeof(inh_msg_bc),
        .gc_bc       = inh_gc_bc,
        .gc_bc_len   = sizeof(inh_gc_bc),
    };
    return elm_build_stub(&def);
}

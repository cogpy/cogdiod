/*
 * member_link_pkg.c — C stub package for MemberLink
 *
 * MemberLink behaviour (Phase 2.7):
 *   - Set membership relation (element ∈ set)
 *   - Participates in abduction (OP_PLN_ABD)
 *   - TV represents membership probability
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t mem_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: abductive reasoning for set membership
 *   Given set properties, abduct member properties
 */
static const uint8_t mem_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = membership TV */
    OP_PLN_ABD,     /* abduction: infer member properties from set */
    OP_ECAN_SP,     /* spread to member and set */
    OP_HALT
};

static const uint8_t mem_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* member_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "MemberLink",
        .init_bc     = mem_init_bc,
        .init_bc_len = sizeof(mem_init_bc),
        .msg_bc      = mem_msg_bc,
        .msg_bc_len  = sizeof(mem_msg_bc),
        .gc_bc       = mem_gc_bc,
        .gc_bc_len   = sizeof(mem_gc_bc),
    };
    return elm_build_stub(&def);
}

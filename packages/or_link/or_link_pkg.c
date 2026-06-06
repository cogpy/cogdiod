/*
 * or_link_pkg.c — C stub package for OrLink
 *
 * OrLink behaviour (Phase 2.9):
 *   - Boolean disjunction of child atoms
 *   - TV = max(strength) of all children
 *   - Fuzzy OR: strength = 1 - product(1 - s_i)
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t or_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: disjunctive TV combination
 *   For OR: strength = max(s1, s2, ...) or fuzzy formula
 *   confidence = min of child confidences
 */
static const uint8_t or_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = current OR TV */
    /* In a full impl: iterate children, take max */
    OP_PLN_REV,
    OP_SET_TV,
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t or_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* or_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "OrLink",
        .init_bc     = or_init_bc,
        .init_bc_len = sizeof(or_init_bc),
        .msg_bc      = or_msg_bc,
        .msg_bc_len  = sizeof(or_msg_bc),
        .gc_bc       = or_gc_bc,
        .gc_bc_len   = sizeof(or_gc_bc),
    };
    return elm_build_stub(&def);
}

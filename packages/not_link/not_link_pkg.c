/*
 * not_link_pkg.c — C stub package for NotLink
 *
 * NotLink behaviour (Phase 2.9):
 *   - Boolean negation of child atom
 *   - TV = (1 - child_strength, child_confidence)
 *   - Complement formula for fuzzy negation
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t not_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: negation TV calculation
 *   NOT(s, c) = (1-s, c)
 *   regs[10..11] = child TV from MSG_SOURCE_CHANGED
 */
static const uint8_t not_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = current NOT TV */
    /* Complement calculation: 1 - strength */
    /* Stub approximates with revision */
    OP_SET_TV,
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t not_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* not_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "NotLink",
        .init_bc     = not_init_bc,
        .init_bc_len = sizeof(not_init_bc),
        .msg_bc      = not_msg_bc,
        .msg_bc_len  = sizeof(not_msg_bc),
        .gc_bc       = not_gc_bc,
        .gc_bc_len   = sizeof(not_gc_bc),
    };
    return elm_build_stub(&def);
}

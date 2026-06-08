/*
 * and_link_pkg.c — C stub package for AndLink
 *
 * AndLink behaviour (Phase 2.9):
 *   - Boolean conjunction of child atoms
 *   - TV = min(strength) of all children, confidence combined
 *   - Fuzzy AND: strength = product of child strengths (when close to 1)
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t and_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: conjunctive TV combination
 *   For AND: strength = min(s1, s2, ...) or product for fuzzy logic
 *   confidence = min of child confidences
 */
static const uint8_t and_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = current AND TV */
    /* In a full impl: iterate children, take min */
    /* Stub: revise with incoming to approximate */
    OP_PLN_REV,
    OP_SET_TV,
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t and_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* and_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "AndLink",
        .init_bc     = and_init_bc,
        .init_bc_len = sizeof(and_init_bc),
        .msg_bc      = and_msg_bc,
        .msg_bc_len  = sizeof(and_msg_bc),
        .gc_bc       = and_gc_bc,
        .gc_bc_len   = sizeof(and_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * grounded_predicate_node_pkg.c — C stub package for GroundedPredicateNode (Phase 2.15)
 *
 * GroundedPredicateNode is a callable atom that evaluates external predicates.
 * Similar to GroundedSchemaNode but returns a boolean/TV result.
 *
 * Behaviour:
 *   init:       Register predicate function pointer
 *   on-message: MSG_INFER evaluates predicate and returns TV result
 *   on-gc:      Unregister from predicate table
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t predicate_gnd_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    OP_HALT
};

/*
 * on-message:
 *   - MSG_INFER: Evaluate the grounded predicate
 *     Returns TV where strength indicates truth value (0.0 = false, 1.0 = true)
 *     and confidence indicates certainty of evaluation
 */
static const uint8_t predicate_gnd_msg_bc[] = {
    OP_GET_TV,
    /* Predicate evaluation would happen here */
    OP_NOP,          /* Placeholder for CALL to grounded predicate */
    OP_SET_TV,       /* Store evaluation result */
    OP_SEND,         /* Send result to caller */
    OP_HALT
};

static const uint8_t predicate_gnd_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* grounded_predicate_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "GroundedPredicateNode",
        .init_bc     = predicate_gnd_init_bc,
        .init_bc_len = sizeof(predicate_gnd_init_bc),
        .msg_bc      = predicate_gnd_msg_bc,
        .msg_bc_len  = sizeof(predicate_gnd_msg_bc),
        .gc_bc       = predicate_gnd_gc_bc,
        .gc_bc_len   = sizeof(predicate_gnd_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * predictive_implication_link_pkg.c — C stub package for PredictiveImplicationLink (Phase 2.12)
 *
 * PredictiveImplicationLink represents temporal implication with a lag parameter.
 * It uses OP_PLN_TMP (temporal deduction) with time decay.
 * Structure: PredictiveImplicationLink(Antecedent, Consequent) + lag stored in AV
 *
 * Behaviour:
 *   init:       Initialize temporal parameters
 *   on-message: MSG_SOURCE_CHANGED triggers temporal deduction with decay
 *   on-gc:      Cleanup
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t pred_impl_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    OP_HALT
};

/*
 * on-message:
 *   - MSG_SOURCE_CHANGED: Apply temporal deduction with time decay
 *   - Uses OP_PLN_TMP which takes: s_ab, c_ab, s_a, c_a, time_steps, decay
 */
static const uint8_t pred_impl_msg_bc[] = {
    OP_GET_TV,         /* Get link TV (implication strength/confidence) */
    /* regs[0]=s_link, regs[1]=c_link */
    /* regs[2..3] = incoming TV from message */
    /* regs[4..5] = time_steps, decay (from AV or constants) */
    OP_PLN_TMP,        /* Temporal deduction: output in regs[6..7] */
    OP_SET_TV,         /* Store result */
    OP_SEND,           /* Send to consequent */
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t pred_impl_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* predictive_implication_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "PredictiveImplicationLink",
        .init_bc     = pred_impl_init_bc,
        .init_bc_len = sizeof(pred_impl_init_bc),
        .msg_bc      = pred_impl_msg_bc,
        .msg_bc_len  = sizeof(pred_impl_msg_bc),
        .gc_bc       = pred_impl_gc_bc,
        .gc_bc_len   = sizeof(pred_impl_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * evaluation_link_pkg.c — C stub package for EvaluationLink
 *
 * EvaluationLink behaviour:
 *   init:       GET_TV → HALT (initialise state from channels)
 *   on-message: GET_TV (self) → PLN_REV with incoming TV → SET_TV → ECAN_SP
 *   on-gc:      NOP → HALT
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t eval_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message for EvaluationLink:
 *   Incoming TV is in regs[10..11] (loaded by elm_exec_msg).
 *   Current TV is loaded by GET_TV into regs[0..1].
 *   We use PLN_REV to revise, then SET_TV, then spread attention.
 */
static const uint8_t eval_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = current TV                    */
    /* regs[10..11] already hold incoming TV from elm_exec_msg    */
    /* For PLN_REV: tv1=regs[0..1], tv2=regs[2..3]               */
    /* Copy regs[10..11] → regs[2..3] (done by the VM setup)     */
    OP_PLN_REV,     /* result → regs[4..5]                        */
    OP_SET_TV,      /* store revised TV                           */
    OP_ECAN_SP,     /* spread attention downstream                */
    OP_HALT
};

static const uint8_t eval_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* evaluation_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "EvaluationLink",
        .init_bc     = eval_init_bc,
        .init_bc_len = sizeof(eval_init_bc),
        .msg_bc      = eval_msg_bc,
        .msg_bc_len  = sizeof(eval_msg_bc),
        .gc_bc       = eval_gc_bc,
        .gc_bc_len   = sizeof(eval_gc_bc),
    };
    return elm_build_stub(&def);
}

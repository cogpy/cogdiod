/*
 * implication_link_pkg.c — C stub package for ImplicationLink
 *
 * ImplicationLink behaviour (PLN Forward Chaining):
 *
 *   init:       GET_TV → HALT
 *
 *   on-message: The core PLN deduction step.
 *     Registers at entry (loaded by elm_exec_msg):
 *       regs[8]  = msg type
 *       regs[9]  = sender UUID
 *       regs[10] = incoming TV strength
 *       regs[11] = incoming TV confidence
 *
 *     Bytecode:
 *       GET_TV          → regs[0..1] = this link's own TV (P(B|A))
 *       PLN_DED         → regs[4..5] = deduce(link_tv, antecedent_tv)
 *                         where antecedent_tv = regs[10..11] from msg
 *       SET_TV          → update own TV with result (optional revision)
 *       ECAN_SP         → spread attention downstream
 *       HALT
 *
 *   on-gc:      NOP → HALT
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t impl_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * PLN Deduction sequence:
 *   regs[0..1] = this link's TV (P(B|A)) — loaded by GET_TV
 *   regs[2..3] = antecedent TV P(A)      — caller copies regs[10..11] here
 *   PLN_DED    → result P(B) in regs[4..5]
 *   ECAN_SP    → spread attention to consequent channel
 */
static const uint8_t impl_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = P(B|A)                    */
    /* regs[2..3] = P(A) — set up by elm_exec_msg from regs[10..11] */
    OP_PLN_DED,     /* deduce(P(B|A), P(A)) → regs[4..5]      */
    /* The deduced TV for B is now in regs[4..5].              */
    /* A full impl would SEND this to the consequent channel.  */
    /* In the stub, ECAN_SP propagates the attention signal.   */
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t impl_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* implication_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "ImplicationLink",
        .init_bc     = impl_init_bc,
        .init_bc_len = sizeof(impl_init_bc),
        .msg_bc      = impl_msg_bc,
        .msg_bc_len  = sizeof(impl_msg_bc),
        .gc_bc       = impl_gc_bc,
        .gc_bc_len   = sizeof(impl_gc_bc),
    };
    return elm_build_stub(&def);
}

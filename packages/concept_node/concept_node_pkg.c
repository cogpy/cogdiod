/*
 * concept_node_pkg.c — C stub package for ConceptNode
 *
 * Until the Elbo compiler is available, this file defines the ConceptNode
 * behaviour directly as Dis VM bytecode sequences and uses elm_build_stub()
 * to assemble them into a loadable ElmPackage.
 *
 * Bytecode sequences implement the logic described in concept_node.elbo:
 *
 *   init:       GET_TV → SET_TV (identity, ensures state is initialised)
 *   on-message: branch on msg type in reg[8]:
 *                 MSG_UPDATE_TV   → PLN_REV (revise TV) → SET_TV → ECAN_SP
 *                 MSG_ATTEND      → GET_STI → ADD delta → SET_STI → ECAN_SP
 *                 MSG_SOURCE_CHANGED → ECAN_SP (re-spread)
 *   on-gc:      NOP → HALT
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

/*
 * init: load own TV into regs, then store back (identity initialisation).
 * In a real implementation this would also register with ECAN.
 */
static const uint8_t concept_init_bc[] = {
    OP_GET_TV,   /* regs[0]=strength, regs[1]=confidence */
    OP_SET_TV,   /* store back (no-op but confirms VM state) */
    OP_HALT
};

/*
 * on-message: a simplified dispatcher.
 *
 * The message type is in regs[8].  We compare and branch:
 *
 *   MSG_UPDATE_TV (0x01):
 *     regs[0..1] already hold incoming TV (loaded by elm_exec_msg)
 *     regs[2..3] = current TV (GET_TV loads into regs[0..1], we copy)
 *     PLN_REV merges them → result in regs[4..5]
 *     Copy regs[4..5] → regs[0..1], then SET_TV
 *     ECAN_SP to spread attention
 *
 *   MSG_ATTEND (0x02):
 *     GET_STI → regs[0]
 *     ADD delta (regs[9] holds sti delta from msg)
 *     SET_STI
 *     ECAN_SP
 *
 *   Default: NOP → HALT
 *
 * Note: this is a linear sequence (no real branching in our stub VM).
 * A real Elbo compiler would emit proper JEQ/JNE instructions.
 * Here we implement the UpdateTV path as the primary path.
 */
static const uint8_t concept_msg_bc[] = {
    /* Step 1: Load current TV into regs[2..3] */
    OP_GET_TV,          /* regs[0]=s, regs[1]=c (current) */
    /* regs[0..1] from msg are the incoming TV (loaded by elm_exec_msg)  */
    /* regs[2..3] = current TV: we use regs[0..1] as tv2 for revision   */
    /* For the stub, we treat regs[0..1] as tv1 and current as tv2      */
    OP_PLN_REV,         /* revise(regs[0..1], regs[2..3]) → regs[4..5]  */
    /* Copy result back to regs[0..1] for SET_TV */
    OP_SET_TV,          /* stores regs[0..1] — NOTE: uses regs[4..5] in */
                        /* a real impl; stub uses regs[0..1] directly    */
    /* Spread attention to neighbours */
    OP_ECAN_SP,
    OP_HALT
};

static const uint8_t concept_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* concept_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "ConceptNode",
        .init_bc     = concept_init_bc,
        .init_bc_len = sizeof(concept_init_bc),
        .msg_bc      = concept_msg_bc,
        .msg_bc_len  = sizeof(concept_msg_bc),
        .gc_bc       = concept_gc_bc,
        .gc_bc_len   = sizeof(concept_gc_bc),
    };
    return elm_build_stub(&def);
}

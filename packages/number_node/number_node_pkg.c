/*
 * number_node_pkg.c — C stub package for NumberNode
 *
 * NumberNode behaviour (Phase 2.2):
 *   - Stores a numeric value in registers (regs[6] = integer, regs[7] = float bits)
 *   - Responds to MSG_CUSTOM with arithmetic operations
 *   - Supports ADD, SUB, MUL, DIV operations
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t num_init_bc[] = {
    OP_LOAD,        /* Load initial value into regs[6] */
    OP_GET_TV,      /* TV can encode value metadata */
    OP_HALT
};

/*
 * on-message: arithmetic operations
 *   regs[8] = msg type (MSG_CUSTOM for arithmetic)
 *   regs[9] = operation code (ADD=1, SUB=2, MUL=3, DIV=4)
 *   regs[10] = operand value
 */
static const uint8_t num_msg_bc[] = {
    OP_LOAD,        /* Load current value into regs[0] */
    OP_ADD,         /* Default: add regs[10] to regs[0] */
    OP_STORE,       /* Store result back */
    OP_ECAN_SP,     /* Spread attention on value change */
    OP_HALT
};

static const uint8_t num_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* number_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "NumberNode",
        .init_bc     = num_init_bc,
        .init_bc_len = sizeof(num_init_bc),
        .msg_bc      = num_msg_bc,
        .msg_bc_len  = sizeof(num_msg_bc),
        .gc_bc       = num_gc_bc,
        .gc_bc_len   = sizeof(num_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * grounded_schema_node_pkg.c — C stub package for GroundedSchemaNode (Phase 2.15)
 *
 * GroundedSchemaNode is a callable atom that executes external code.
 * It stores a function pointer (via kernel_ref) and a registration table entry.
 *
 * The ep_init stores the function reference; ep_on_message dispatches
 * MSG_INFER to execute the grounded schema with its arguments.
 *
 * Behaviour:
 *   init:       Register function pointer in kernel's schema table
 *   on-message: MSG_INFER executes the schema, returns result TV
 *   on-gc:      Unregister from schema table
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Bytecode sequences ───────────────────────────────────────────────── */

static const uint8_t schema_init_bc[] = {
    OP_GET_TV,
    OP_SET_TV,
    /* kernel_ref would be set to function pointer by spawn caller */
    OP_HALT
};

/*
 * on-message:
 *   - MSG_INFER: Execute the grounded schema
 *     In a real implementation, this would:
 *     1. Load arguments from the incoming ExecutionLink
 *     2. Call the registered C function via kernel_ref
 *     3. Store result in regs and return as TV
 */
static const uint8_t schema_msg_bc[] = {
    OP_GET_TV,
    /* Execution would happen here via kernel callback */
    OP_NOP,          /* Placeholder for CALL to grounded function */
    OP_SET_TV,       /* Store execution result */
    OP_SEND,         /* Send result to caller */
    OP_HALT
};

static const uint8_t schema_gc_bc[] = {
    /* Unregister from schema table */
    OP_NOP,
    OP_HALT
};

/* ── Package builder ──────────────────────────────────────────────────── */

ElmPackage* grounded_schema_node_build_package(void) {
    ElmStubDef def = {
        .type_name   = "GroundedSchemaNode",
        .init_bc     = schema_init_bc,
        .init_bc_len = sizeof(schema_init_bc),
        .msg_bc      = schema_msg_bc,
        .msg_bc_len  = sizeof(schema_msg_bc),
        .gc_bc       = schema_gc_bc,
        .gc_bc_len   = sizeof(schema_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * similarity_link_pkg.c — C stub package for SimilarityLink
 *
 * SimilarityLink behaviour (Phase 2.6):
 *   - Symmetric relation between concepts
 *   - Uses PLN_REV-based TV combination
 *   - Bidirectional: A~B implies B~A
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>

static const uint8_t sim_init_bc[] = {
    OP_GET_TV,
    OP_HALT
};

/*
 * on-message: symmetric PLN revision
 *   When either endpoint changes, revise similarity TV
 *   Uses PLN_REV because similarity evidence combines symmetrically
 */
static const uint8_t sim_msg_bc[] = {
    OP_GET_TV,      /* regs[0..1] = current similarity TV */
    OP_PLN_REV,     /* revise with incoming evidence */
    OP_SET_TV,      /* update similarity */
    OP_ECAN_SP,     /* spread to both endpoints */
    OP_HALT
};

static const uint8_t sim_gc_bc[] = {
    OP_NOP,
    OP_HALT
};

ElmPackage* similarity_link_build_package(void) {
    ElmStubDef def = {
        .type_name   = "SimilarityLink",
        .init_bc     = sim_init_bc,
        .init_bc_len = sizeof(sim_init_bc),
        .msg_bc      = sim_msg_bc,
        .msg_bc_len  = sizeof(sim_msg_bc),
        .gc_bc       = sim_gc_bc,
        .gc_bc_len   = sizeof(sim_gc_bc),
    };
    return elm_build_stub(&def);
}

/*
 * fuzz_distyx.c — LibFuzzer target for DisTyx request dispatch
 *
 * Compile: clang -O1 -fsanitize=fuzzer,address -std=c11 -Iinclude \
 *              -D_GNU_SOURCE -DDISVM_NREGS=16 -DDISVM_STKMAX=4096 \
 *              src/kernel/cogdiod_kernel.c src/kernel/pln.c \
 *              src/kernel/cogdiod_log.c src/p9/distyx.c \
 *              src/elbo/elm_loader.c src/elbo/elbo_compiler.c \
 *              packages/concept_node/concept_node_pkg.c \
 *              packages/evaluation_link/evaluation_link_pkg.c \
 *              packages/implication_link/implication_link_pkg.c \
 *              tests/fuzz_distyx.c -lm -lpthread -o fuzz_distyx
 *
 * Run: ./fuzz_distyx -max_total_time=60
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "cogdiod.h"
#include "distyx.h"

ElmPackage* concept_node_build_package(void);

/* One shared kernel (re-init per fuzz iteration would be too slow) */
static CogDiodKernel* gk = NULL;

__attribute__((constructor))
static void fuzz_init(void) {
    gk = cogdiod_create(0, 1);
    ElmPackage* cn = concept_node_build_package();
    pthread_mutex_lock(&gk->pkg_lock);
    gk->pkg_cache[cn->type_id % PKG_CACHE_BUCKETS] = cn;
    gk->pkg_count++;
    pthread_mutex_unlock(&gk->pkg_lock);
    /* Pre-populate a few atoms so path handlers find real UUIDs */
    cogdiod_spawn(gk, "ConceptNode", "fuzz_a");
    cogdiod_spawn(gk, "ConceptNode", "fuzz_b");
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;

    DisTyxRequest req = {0};
    DisTyxResponse resp = {0};

    /* First byte selects the operation */
    req.op = data[0] % 7;  /* 0..6 covers all DT_OP_* values */

    /* Rest is the path (null-terminated within the struct bounds) */
    size_t plen = size - 1;
    if (plen >= sizeof(req.path)) plen = sizeof(req.path) - 1;
    memcpy(req.path, data + 1, plen);
    req.path[plen] = '\0';

    /* Feed some bytes as write payload */
    if (size > 16) {
        size_t dlen = size - 16;
        if (dlen > sizeof(req.buf)) dlen = sizeof(req.buf);
        memcpy(req.buf, data + 16, dlen);
        req.buf_len = dlen;
    }

    distyx_dispatch(gk, &req, &resp);
    return 0;
}

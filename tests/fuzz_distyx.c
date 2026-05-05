/*
 * fuzz_distyx.c — libFuzzer harness for distyx_dispatch path parsing
 *
 * Build (requires clang with libFuzzer):
 *   clang -fsanitize=fuzzer,address -O1 -std=c11 -Iinclude \
 *         tests/fuzz_distyx.c src/kernel/cogdiod_kernel.c \
 *         src/kernel/cogdiod_log.c src/kernel/pln.c \
 *         src/p9/distyx.c src/elbo/elm_loader.c src/elbo/elbo_compiler.c \
 *         packages/concept_node/concept_node_pkg.c \
 *         packages/implication_link/implication_link_pkg.c \
 *         packages/evaluation_link/evaluation_link_pkg.c \
 *         -o fuzz_distyx -lpthread -lm
 *
 * Run:
 *   ./fuzz_distyx -max_len=512 -runs=100000
 */

#include "cogdiod.h"
#include "distyx.h"
#include <string.h>
#include <stdlib.h>

/* Global kernel: created once, reused across fuzz iterations */
static CogDiodKernel* g_kernel = NULL;

/* Package builder forward decls */
ElmPackage* concept_node_build_package(void);

static void __attribute__((constructor)) init_kernel(void) {
    g_kernel = cogdiod_create(0, 0);
    if (!g_kernel) return;

    /* Load the ConceptNode package */
    ElmPackage* cn = concept_node_build_package();
    if (cn) {
        uint32_t b = cn->type_id % PKG_CACHE_BUCKETS;
        pthread_mutex_lock(&g_kernel->pkg_lock);
        cn->next_in_cache = g_kernel->pkg_cache[b];
        g_kernel->pkg_cache[b] = cn;
        g_kernel->pkg_count++;
        pthread_mutex_unlock(&g_kernel->pkg_lock);
    }

    /* Spawn a couple of atoms so UUIDs 1 and 2 exist */
    cogdiod_spawn(g_kernel, "ConceptNode", "fuzz_a");
    cogdiod_spawn(g_kernel, "ConceptNode", "fuzz_b");
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!g_kernel || size == 0) return 0;

    /* Build a null-terminated path from the fuzz input */
    size_t plen = size < 511 ? size : 511;
    char path[512];
    memcpy(path, data, plen);
    path[plen] = '\0';

    /* Try all operation types */
    static const int ops[] = {
        DT_OP_READ, DT_OP_WRITE, DT_OP_CREATE,
        DT_OP_REMOVE, DT_OP_LINK, DT_OP_STAT,
    };
    int op = ops[size % (sizeof(ops)/sizeof(ops[0]))];

    DisTyxRequest  req  = {0};
    DisTyxResponse resp = {0};
    req.op = op;
    strncpy(req.path, path, sizeof(req.path) - 1);

    /* For write ops: fill buf with fuzz data */
    if (op == DT_OP_WRITE && size > plen) {
        size_t dlen = size - plen;
        if (dlen > sizeof(req.buf)) dlen = sizeof(req.buf);
        memcpy(req.buf, data + plen, dlen);
        req.buf_len = dlen;
    }

    /* Should not crash */
    distyx_dispatch(g_kernel, &req, &resp);
    return 0;
}

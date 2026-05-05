/*
 * fuzz_elbo.c — libFuzzer harness for the Elbo compiler
 *
 * Feeds arbitrary bytes as Elbo source code to elbo_compile().
 * Any crash, assertion failure, or memory error is a bug.
 *
 * Build (requires clang with libFuzzer):
 *   clang -fsanitize=fuzzer,address -O1 -std=c11 -Iinclude \
 *         tests/fuzz_elbo.c src/elbo/elbo_compiler.c \
 *         src/elbo/elm_loader.c src/kernel/cogdiod_log.c \
 *         src/kernel/pln.c src/kernel/cogdiod_kernel.c \
 *         packages/concept_node/concept_node_pkg.c \
 *         packages/implication_link/implication_link_pkg.c \
 *         packages/evaluation_link/evaluation_link_pkg.c \
 *         -o fuzz_elbo -lpthread -lm
 *
 * Run:
 *   ./fuzz_elbo -max_len=1024 -runs=500000
 */

#include "elbo_compiler.h"
#include "cogdiod.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    /* Make a null-terminated copy of the fuzz input */
    char* src = malloc(size + 1);
    if (!src) return 0;
    memcpy(src, data, size);
    src[size] = '\0';

    /* Compile it — should never crash regardless of input */
    ElmPackage* pkg = elbo_compile(src, "FuzzAtom");
    if (pkg) {
        /* Optionally run a few VM steps to exercise the executor */
        if (pkg->bytecode_size > 0) {
            AtomIsolate a = {0};
            a.uuid    = 1;
            a.package = pkg;
            a.tv      = (TruthValue){ 0.5f, 0.5f };
            a.vm_ctx.stack     = calloc(DISVM_STKMAX, 1);
            a.vm_ctx.heap      = calloc(4096, 1);
            a.vm_ctx.heap_size = 4096;
            pthread_mutex_init(&a.lock, NULL);
            pthread_mutex_lock(&pkg->ref_lock);
            pkg->ref_count++;
            pthread_mutex_unlock(&pkg->ref_lock);

            /* Run init — limit steps by using bytecode as-is */
            elm_exec_init(&a);

            pthread_mutex_lock(&pkg->ref_lock);
            pkg->ref_count--;
            pthread_mutex_unlock(&pkg->ref_lock);
            pthread_mutex_destroy(&a.lock);
            free(a.vm_ctx.stack);
            free(a.vm_ctx.heap);
        }
        free(pkg->dis_bytecode);
        pthread_mutex_destroy(&pkg->ref_lock);
        free(pkg);
    }

    free(src);
    return 0;
}

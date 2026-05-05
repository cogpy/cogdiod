/*
 * fuzz_elbo.c — LibFuzzer target for the Elbo S-expr compiler
 *
 * Compile: clang -O1 -fsanitize=fuzzer,address -std=c11 -Iinclude \
 *              -D_GNU_SOURCE -DDISVM_NREGS=16 -DDISVM_STKMAX=4096 \
 *              src/elbo/elbo_compiler.c src/elbo/elm_loader.c \
 *              src/kernel/cogdiod_kernel.c src/kernel/pln.c \
 *              src/kernel/cogdiod_log.c src/p9/distyx.c \
 *              packages/concept_node/concept_node_pkg.c \
 *              packages/evaluation_link/evaluation_link_pkg.c \
 *              packages/implication_link/implication_link_pkg.c \
 *              tests/fuzz_elbo.c -lm -lpthread -o fuzz_elbo
 *
 * Run: ./fuzz_elbo -max_total_time=60
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "elbo_compiler.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    /* Make a null-terminated copy of the fuzz input */
    char* src = malloc(size + 1);
    if (!src) return 0;
    memcpy(src, data, size);
    src[size] = '\0';

    ElmPackage* pkg = elbo_compile(src, "FuzzType");
    if (pkg) {
        free(pkg->dis_bytecode);
        free(pkg);
    }
    free(src);
    return 0;
}

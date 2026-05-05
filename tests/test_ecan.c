/*
 * test_ecan.c — Unit tests for ECAN (Economic Attention Allocation)
 *
 * Compile: cc -O2 -std=c11 -Iinclude -D_GNU_SOURCE \
 *              -DDISVM_NREGS=16 -DDISVM_STKMAX=4096 \
 *              src/kernel/cogdiod_kernel.c src/kernel/pln.c \
 *              src/kernel/cogdiod_log.c \
 *              src/p9/distyx.c src/elbo/elm_loader.c \
 *              src/elbo/elbo_compiler.c \
 *              packages/concept_node/concept_node_pkg.c \
 *              packages/evaluation_link/evaluation_link_pkg.c \
 *              packages/implication_link/implication_link_pkg.c \
 *              tests/test_ecan.c -lm -lpthread -o test_ecan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "cogdiod.h"

static int pass = 0, fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { printf("PASS: %s\n", msg); pass++; } \
    else       { printf("FAIL: %s (line %d)\n", msg, __LINE__); fail++; } \
} while(0)

#define CHECK_NEAR(a, b, eps, msg) \
    CHECK(fabs((double)(a)-(double)(b)) < (eps), msg)

/* ── Helpers ──────────────────────────────────────────────────────────── */

ElmPackage* concept_node_build_package(void);
ElmPackage* implication_link_build_package(void);
ElmPackage* evaluation_link_build_package(void);

static CogDiodKernel* make_kernel(void) {
    CogDiodKernel* k = cogdiod_create(0, 2);
    pthread_mutex_lock(&k->pkg_lock);
    ElmPackage* cn = concept_node_build_package();
    k->pkg_cache[cn->type_id % PKG_CACHE_BUCKETS] = cn;
    ElmPackage* il = implication_link_build_package();
    k->pkg_cache[il->type_id % PKG_CACHE_BUCKETS] = il;
    ElmPackage* el = evaluation_link_build_package();
    k->pkg_cache[el->type_id % PKG_CACHE_BUCKETS] = el;
    k->pkg_count += 3;
    pthread_mutex_unlock(&k->pkg_lock);
    return k;
}

/* ── Tests ────────────────────────────────────────────────────────────── */

static void test_sti_update(void) {
    printf("\n[ecan] STI update via cogdiod_attend\n");
    CogDiodKernel* k = make_kernel();

    uint64_t uuid = cogdiod_spawn(k, "ConceptNode", "focus")->uuid;
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    CHECK(a != NULL, "spawn focus atom");

    float old_sti = a->av.sti;
    cogdiod_attend(k, uuid, 5.0f);

    AtomIsolate* b = cogdiod_get_atom(k, uuid);
    CHECK(b != NULL, "atom still accessible after attend");
    CHECK(b->av.sti > old_sti, "STI increased after attend");

    cogdiod_stop(k);
    free(k);
}

static void test_sti_decay(void) {
    printf("\n[ecan] STI decay: ECAN diffusion spreads STI to linked neighbour\n");
    CogDiodKernel* k = make_kernel();
    cogdiod_start(k);

    uint64_t u1 = cogdiod_spawn(k, "ConceptNode", "src")->uuid;
    uint64_t u2 = cogdiod_spawn(k, "ConceptNode", "dst")->uuid;
    cogdiod_link(k, u1, u2);   /* u1 has outgoing link → u2 */

    AtomIsolate* a1 = cogdiod_get_atom(k, u1);
    CHECK(a1 != NULL, "spawn src atom");

    /* Set src STI to 10.0 */
    cogdiod_attend(k, u1, 10.0f);
    float after_attend = cogdiod_get_atom(k, u1)->av.sti;
    CHECK(after_attend > 5.0f, "STI set to ~10 via attend");

    usleep(350000);  /* 350 ms — at least 3 ECAN ticks (100ms each) */

    AtomIsolate* b = cogdiod_get_atom(k, u1);
    /* After spread, src STI should have decreased */
    CHECK(b->av.sti < after_attend, "src STI decreased after spread");

    cogdiod_stop(k);
    free(k);
}

static void test_hebbian_weight(void) {
    printf("\n[ecan] Hebbian weight update\n");
    CogDiodKernel* k = make_kernel();

    uint64_t u1 = cogdiod_spawn(k, "ConceptNode", "h1")->uuid;
    uint64_t u2 = cogdiod_spawn(k, "ConceptNode", "h2")->uuid;

    cogdiod_link(k, u1, u2);

    AtomIsolate* a1 = cogdiod_get_atom(k, u1);
    AtomIsolate* a2 = cogdiod_get_atom(k, u2);
    CHECK(a1 && a2, "both hebbian atoms accessible");

    /* Simulate co-activation a few times */
    for (int i = 0; i < 3; i++)
        cogdiod_hebbian_update(k, u1, u2);

    /* Hebbian weight on outgoing channel should have increased */
    pthread_mutex_lock(&a1->lock);
    LimboChannel* ch = a1->outgoing;
    float w = ch ? ch->weight : 0.0f;
    pthread_mutex_unlock(&a1->lock);

    CHECK(w > 0.0f, "Hebbian weight increased after co-activation");

    cogdiod_stop(k);
    free(k);
}

static void test_tv_history(void) {
    printf("\n[ecan] TV episodic history\n");
    CogDiodKernel* k = make_kernel();

    uint64_t uuid = cogdiod_spawn(k, "ConceptNode", "hist")->uuid;
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    CHECK(a != NULL, "spawn hist atom");

    int before = a->history_count;
    cogdiod_set_tv(k, uuid, (TruthValue){0.7f, 0.8f});
    cogdiod_set_tv(k, uuid, (TruthValue){0.6f, 0.7f});

    AtomIsolate* b = cogdiod_get_atom(k, uuid);
    CHECK(b->history_count >= before + 2, "history_count incremented");

    cogdiod_stop(k);
    free(k);
}

int main(void) {
    printf("=== ECAN Unit Tests ===\n");
    test_sti_update();
    test_sti_decay();
    test_hebbian_weight();
    test_tv_history();
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}

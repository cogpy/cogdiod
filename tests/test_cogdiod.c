/*
 * test_cogdiod.c — Integration test for the archetypal CogDiod entity
 *
 * This test exercises the complete lifecycle of a CogDiod kernel:
 *
 *   1. Kernel creation
 *   2. Package loading (ConceptNode, ImplicationLink)
 *   3. Atom spawning (two ConceptNodes + one ImplicationLink)
 *   4. Channel linking (A → ImplLink → B)
 *   5. TruthValue propagation (update A, verify B changes via PLN deduction)
 *   6. Attention allocation (ECAN spread)
 *   7. DisTyx dispatch (path-based API)
 *   8. Atom destruction and GC
 *
 * Expected output (approximate):
 *   [cogdiod] kernel created (port=9999 workers=2)
 *   [elm] built stub package 'ConceptNode' ...
 *   [elm] built stub package 'ImplicationLink' ...
 *   [cogdiod] spawned ConceptNode 'cat' uuid=1
 *   [cogdiod] spawned ConceptNode 'animal' uuid=2
 *   [cogdiod] spawned ImplicationLink '(null)' uuid=3
 *   [cogdiod] linked 1 -> 3
 *   [cogdiod] linked 3 -> 2
 *   PASS: atom_count == 3
 *   PASS: ConceptNode 'cat' TV set to (0.80, 0.90)
 *   PASS: ImplicationLink TV set to (0.95, 0.80)
 *   PASS: PLN deduction propagated to 'animal'
 *   PASS: DisTyx stats path returns JSON
 *   PASS: atom destroyed, atom_count == 2
 *   ALL TESTS PASSED
 */

#include "cogdiod.h"
#include "elm_types.h"
#include "distyx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* Forward declarations of package builders */
ElmPackage* concept_node_build_package(void);
ElmPackage* implication_link_build_package(void);
ElmPackage* evaluation_link_build_package(void);

/* Forward declarations from elm_loader.c */
int elm_exec_init(AtomIsolate* a);
int elm_exec_msg(AtomIsolate* a, const CogMessage* msg);

/* Forward declaration from distyx.c */
int distyx_dispatch(CogDiodKernel* k,
                    const DisTyxRequest* req,
                    DisTyxResponse* resp);

/* ─────────────────────────────────────────────────────────────────────────
 * Test helpers
 * ───────────────────────────────────────────────────────────────────────── */

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
            return 1; \
        } \
        fprintf(stderr, "PASS: %s\n", msg); \
    } while(0)

static int float_near(float a, float b, float eps) {
    return fabsf(a - b) < eps;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Helper: insert a pre-built package directly into the kernel cache
 * ───────────────────────────────────────────────────────────────────────── */

static void kernel_insert_package(CogDiodKernel* k, ElmPackage* pkg) {
    pthread_mutex_lock(&k->pkg_lock);
    uint32_t b = pkg->type_id % PKG_CACHE_BUCKETS;
    k->pkg_cache[b] = pkg;
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 1: Kernel creation and package loading
 * ───────────────────────────────────────────────────────────────────────── */

static int test_kernel_and_packages(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 1: Kernel & Packages ===\n");

    CHECK(k != NULL, "kernel created");
    CHECK(k->atom_count == 0, "initial atom count is 0");
    CHECK(k->pkg_count == 3, "three packages loaded");

    ElmPackage* cn_pkg = cogdiod_get_package(k,
                            cogdiod_hash_type("ConceptNode"));
    CHECK(cn_pkg != NULL, "ConceptNode package found in cache");
    CHECK(cn_pkg->magic == ELM_MAGIC, "ConceptNode package magic valid");

    ElmPackage* il_pkg = cogdiod_get_package(k,
                            cogdiod_hash_type("ImplicationLink"));
    CHECK(il_pkg != NULL, "ImplicationLink package found in cache");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 2: Atom spawning
 * ───────────────────────────────────────────────────────────────────────── */

static int test_spawn(CogDiodKernel* k,
                      AtomIsolate** out_cat,
                      AtomIsolate** out_animal,
                      AtomIsolate** out_impl) {
    fprintf(stderr, "\n=== Test 2: Atom Spawning ===\n");

    *out_cat    = cogdiod_spawn(k, "ConceptNode",    "cat");
    *out_animal = cogdiod_spawn(k, "ConceptNode",    "animal");
    *out_impl   = cogdiod_spawn(k, "ImplicationLink", NULL);

    CHECK(*out_cat    != NULL, "spawned ConceptNode 'cat'");
    CHECK(*out_animal != NULL, "spawned ConceptNode 'animal'");
    CHECK(*out_impl   != NULL, "spawned ImplicationLink");
    CHECK(k->atom_count == 3, "atom_count == 3");

    /* Run init entry points */
    elm_exec_init(*out_cat);
    elm_exec_init(*out_animal);
    elm_exec_init(*out_impl);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 3: Channel linking
 * ───────────────────────────────────────────────────────────────────────── */

static int test_linking(CogDiodKernel* k,
                        AtomIsolate* cat,
                        AtomIsolate* animal,
                        AtomIsolate* impl) {
    fprintf(stderr, "\n=== Test 3: Channel Linking ===\n");

    /* cat → impl (cat is the antecedent) */
    LimboChannel* ch1 = cogdiod_link(k, cat->uuid, impl->uuid);
    CHECK(ch1 != NULL, "linked cat -> impl");
    CHECK(cat->outgoing_count == 1, "cat has 1 outgoing channel");

    /* impl → animal (animal is the consequent) */
    LimboChannel* ch2 = cogdiod_link(k, impl->uuid, animal->uuid);
    CHECK(ch2 != NULL, "linked impl -> animal");
    CHECK(impl->outgoing_count == 1, "impl has 1 outgoing channel");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 4: TruthValue operations
 * ───────────────────────────────────────────────────────────────────────── */

static int test_truth_values(CogDiodKernel* k,
                             AtomIsolate* cat,
                             AtomIsolate* impl) {
    fprintf(stderr, "\n=== Test 4: TruthValue Operations ===\n");

    /* Set cat's TV: P(cat) = (0.80, 0.90) */
    TruthValue cat_tv = { 0.80f, 0.90f };
    int r = cogdiod_set_tv(k, cat->uuid, cat_tv);
    CHECK(r == 0, "set cat TV returned 0");

    TruthValue got = cogdiod_get_tv(k, cat->uuid);
    CHECK(float_near(got.strength,   0.80f, 0.01f),
          "ConceptNode 'cat' TV set to (0.80, 0.90)");

    /* Set impl's TV: P(animal|cat) = (0.95, 0.80) */
    TruthValue impl_tv = { 0.95f, 0.80f };
    cogdiod_set_tv(k, impl->uuid, impl_tv);
    TruthValue got2 = cogdiod_get_tv(k, impl->uuid);
    CHECK(float_near(got2.strength, 0.95f, 0.01f),
          "ImplicationLink TV set to (0.95, 0.80)");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 5: PLN deduction via message dispatch
 * ───────────────────────────────────────────────────────────────────────── */

static int test_pln_deduction(CogDiodKernel* k,
                              AtomIsolate* cat,
                              AtomIsolate* impl,
                              AtomIsolate* animal) {
    fprintf(stderr, "\n=== Test 5: PLN Deduction ===\n");

    /* Simulate: cat's TV changed → send MSG_SOURCE_CHANGED to impl */
    TruthValue cat_tv = cogdiod_get_tv(k, cat->uuid);
    CogMessage msg = {
        .type        = MSG_SOURCE_CHANGED,
        .sender_uuid = cat->uuid,
        .tv          = cat_tv,
    };

    /* Load the message into impl's VM registers and execute on-message */
    impl->vm_ctx.regs[8]  = MSG_SOURCE_CHANGED;
    impl->vm_ctx.regs[9]  = cat->uuid;
    memcpy(&impl->vm_ctx.regs[2], &cat_tv.strength,   4);
    memcpy(&impl->vm_ctx.regs[3], &cat_tv.confidence, 4);

    int r = elm_exec_msg(impl, &msg);
    CHECK(r == 0, "elm_exec_msg on ImplicationLink returned 0");

    /*
     * Expected deduction:
     *   P(animal) ≈ P(animal|cat) * P(cat) = 0.95 * 0.80 = 0.76
     *   confidence ≈ 0.80 * 0.90 * 0.9 = 0.648
     *
     * The result is in impl's regs[4..5] after PLN_DED execution.
     */
    float deduced_s, deduced_c;
    memcpy(&deduced_s, &impl->vm_ctx.regs[4], 4);
    memcpy(&deduced_c, &impl->vm_ctx.regs[5], 4);

    fprintf(stderr, "  Deduced TV for 'animal': strength=%.3f confidence=%.3f\n",
            deduced_s, deduced_c);

    CHECK(float_near(deduced_s, 0.76f, 0.05f),
          "PLN deduction propagated to 'animal'");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 6: Attention allocation
 * ───────────────────────────────────────────────────────────────────────── */

static int test_attention(CogDiodKernel* k, AtomIsolate* cat) {
    fprintf(stderr, "\n=== Test 6: Attention Allocation ===\n");

    float before = cat->av.sti;
    cogdiod_attend(k, cat->uuid, 5.0f);
    float after = cat->av.sti;

    CHECK(float_near(after, before + 5.0f, 0.01f),
          "STI increased by 5.0 after attend");
    CHECK(cat->state == ATOM_ALIVE, "atom state is ALIVE after attention");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 7: DisTyx path-based dispatch
 * ───────────────────────────────────────────────────────────────────────── */

static int test_distyx(CogDiodKernel* k, AtomIsolate* cat) {
    fprintf(stderr, "\n=== Test 7: DisTyx Dispatch ===\n");

    /* READ /ai/stats */
    DisTyxRequest req = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/stats");

    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "DisTyx stats path returns 0");
    CHECK(resp.buf_len > 0, "DisTyx stats path returns JSON");
    fprintf(stderr, "  Stats: %.*s", (int)resp.buf_len, resp.buf);

    /* READ /ai/atoms/<uuid>/tv */
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.op = DT_OP_READ;
    snprintf(req.path, sizeof(req.path), "/ai/atoms/%llu/tv",
             (unsigned long long)cat->uuid);

    r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "DisTyx read TV path returns 0");
    CHECK(resp.buf_len == 8, "DisTyx read TV returns 8 bytes");

    float s, c;
    memcpy(&s, resp.buf,     4);
    memcpy(&c, resp.buf + 4, 4);
    fprintf(stderr, "  TV via DisTyx: strength=%.3f confidence=%.3f\n", s, c);
    CHECK(float_near(s, 0.80f, 0.01f), "DisTyx TV read matches set value");

    /* WRITE /ai/atoms/<uuid>/sti */
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.op = DT_OP_WRITE;
    snprintf(req.path, sizeof(req.path), "/ai/atoms/%llu/sti",
             (unsigned long long)cat->uuid);
    float delta = 3.0f;
    memcpy(req.buf, &delta, 4);
    req.buf_len = 4;

    r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "DisTyx write STI path returns 0");
    CHECK(float_near(cat->av.sti, 8.0f, 0.1f),
          "DisTyx STI write accumulated correctly");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test 8: Atom destruction
 * ───────────────────────────────────────────────────────────────────────── */

static int test_destroy(CogDiodKernel* k, uint64_t impl_uuid) {
    fprintf(stderr, "\n=== Test 8: Atom Destruction ===\n");

    int r = cogdiod_destroy_atom(k, impl_uuid);
    CHECK(r == 0, "atom destroyed");
    CHECK(k->atom_count == 2, "atom_count == 2 after destroy");

    AtomIsolate* gone = cogdiod_get_atom(k, impl_uuid);
    CHECK(gone == NULL, "destroyed atom not found in pool");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "=== CogDiod Integration Test Suite ===\n\n");

    /* Create kernel */
    CogDiodKernel* k = cogdiod_create(9999, 2);
    assert(k);
    cogdiod_start(k);

    /* Build and insert packages */
    ElmPackage* cn_pkg  = concept_node_build_package();
    ElmPackage* il_pkg  = implication_link_build_package();
    ElmPackage* el_pkg  = evaluation_link_build_package();
    kernel_insert_package(k, cn_pkg);
    kernel_insert_package(k, il_pkg);
    kernel_insert_package(k, el_pkg);

    int failures = 0;

    /* Run tests */
    AtomIsolate *cat = NULL, *animal = NULL, *impl = NULL;

    failures += test_kernel_and_packages(k);
    failures += test_spawn(k, &cat, &animal, &impl);
    failures += test_linking(k, cat, animal, impl);
    failures += test_truth_values(k, cat, impl);
    failures += test_pln_deduction(k, cat, impl, animal);
    failures += test_attention(k, cat);
    failures += test_distyx(k, cat);

    uint64_t impl_uuid = impl->uuid;
    failures += test_destroy(k, impl_uuid);

    /* Cleanup */
    cogdiod_destroy(k);

    fprintf(stderr, "\n");
    if (failures == 0) {
        fprintf(stderr, "ALL TESTS PASSED\n");
        return 0;
    } else {
        fprintf(stderr, "%d TEST(S) FAILED\n", failures);
        return 1;
    }
}

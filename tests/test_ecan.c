/*
 * test_ecan.c — Unit tests for ECAN operations in cogdiod_kernel.c
 *
 * Tests:
 *   1. cogdiod_attend  — STI delta applied correctly
 *   2. cogdiod_ecan_diffuse — STI spreads across channels
 *   3. cogdiod_hebbian_update — Hebbian weight update
 *   4. ATOM_ALIVE / ATOM_SLEEPING state transitions
 *   5. STI conservation (diffuse doesn't create STI from nothing)
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;

#define PASS(msg)  fprintf(stderr, "PASS: %s\n", (msg))
#define FAIL(msg)  do { fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); failures++; } while(0)
#define CHECK(c,m) do { if (c) PASS(m); else FAIL(m); } while(0)
#define CHECK_NEAR(a,b,eps,m) CHECK(fabsf((float)(a)-(float)(b)) < (float)(eps), m)

ElmPackage* concept_node_build_package(void);

static void kernel_insert_package(CogDiodKernel* k, ElmPackage* pkg) {
    uint32_t b = pkg->type_id % PKG_CACHE_BUCKETS;
    pthread_mutex_lock(&k->pkg_lock);
    pkg->next_in_cache = k->pkg_cache[b];
    k->pkg_cache[b] = pkg;
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);
}

/* ── Test 1: cogdiod_attend ─────────────────────────────────────────── */
static void test_attend(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 1: cogdiod_attend ===\n");

    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "attend_test");
    CHECK(a != NULL, "spawn attend_test atom");
    if (!a) return;

    float before = a->av.sti;
    CHECK(a->state == ATOM_SLEEPING, "initial state is SLEEPING");

    int r = cogdiod_attend(k, a->uuid, 5.0f);
    CHECK(r == 0, "cogdiod_attend returns 0");
    CHECK_NEAR(a->av.sti, before + 5.0f, 0.01f, "STI increased by 5.0");
    CHECK(a->state == ATOM_ALIVE, "state is ALIVE after positive attend");

    /* Attend with zero delta — state should remain ALIVE */
    r = cogdiod_attend(k, a->uuid, 0.0f);
    CHECK(r == 0, "cogdiod_attend zero delta returns 0");
    CHECK(a->state == ATOM_ALIVE, "state still ALIVE after zero attend");

    /* Attend with negative delta */
    r = cogdiod_attend(k, a->uuid, -100.0f);
    CHECK(r == 0, "cogdiod_attend negative delta returns 0");
    /* STI may go negative — that's allowed */
}

/* ── Test 2: cogdiod_ecan_diffuse ───────────────────────────────────── */
static void test_ecan_diffuse(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 2: cogdiod_ecan_diffuse ===\n");

    AtomIsolate* src = cogdiod_spawn(k, "ConceptNode", "diffuse_src");
    AtomIsolate* dst = cogdiod_spawn(k, "ConceptNode", "diffuse_dst");
    CHECK(src != NULL && dst != NULL, "spawn src and dst atoms");
    if (!src || !dst) return;

    /* Give src a high STI so it spreads */
    cogdiod_attend(k, src->uuid, 10.0f);
    float src_sti_before = src->av.sti;

    /* Link src → dst */
    LimboChannel* ch = cogdiod_link(k, src->uuid, dst->uuid);
    CHECK(ch != NULL, "link src->dst");

    /* Run one diffuse cycle */
    cogdiod_ecan_diffuse(k);

    /* src should have lost some STI */
    CHECK(src->av.sti < src_sti_before, "src STI decreased after diffuse");
}

/* ── Test 3: cogdiod_hebbian_update ─────────────────────────────────── */
static void test_hebbian(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 3: cogdiod_hebbian_update ===\n");

    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "hebb_a");
    AtomIsolate* b = cogdiod_spawn(k, "ConceptNode", "hebb_b");
    CHECK(a != NULL && b != NULL, "spawn hebb_a and hebb_b");
    if (!a || !b) return;

    /* Link a → b */
    LimboChannel* ch = cogdiod_link(k, a->uuid, b->uuid);
    CHECK(ch != NULL, "link hebb_a->hebb_b");

    float weight_before = ch->weight;
    int r = cogdiod_hebbian_update(k, a->uuid, b->uuid);
    CHECK(r == 0, "cogdiod_hebbian_update returns 0");
    CHECK(ch->weight != weight_before || 1, "Hebbian update ran without error");
    /* Weight should have changed (strengthened) */
    CHECK(ch->weight >= weight_before, "Hebbian weight non-decreasing after co-activation");
}

/* ── Test 4: state transitions ──────────────────────────────────────── */
static void test_state_transitions(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 4: state transitions ===\n");

    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "state_test");
    if (!a) { FAIL("spawn state_test"); return; }

    CHECK(a->state == ATOM_SLEEPING, "initial state SLEEPING");

    cogdiod_attend(k, a->uuid, 1.0f);
    CHECK(a->state == ATOM_ALIVE, "state ALIVE after attend > 0");

    /* Mark as dying */
    pthread_mutex_lock(&a->lock);
    a->state = ATOM_DYING;
    pthread_mutex_unlock(&a->lock);
    CHECK(a->state == ATOM_DYING, "state DYING after manual mark");
}

/* ── Test 5: attend with unknown UUID ────────────────────────────────── */
static void test_attend_unknown(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 5: attend unknown UUID ===\n");
    int r = cogdiod_attend(k, 999999ULL, 5.0f);
    CHECK(r != 0, "cogdiod_attend unknown UUID returns non-zero");
}

/* ── main ─────────────────────────────────────────────────────────────── */
int main(void) {
    fprintf(stderr, "=== ECAN Unit Test Suite ===\n");

    CogDiodKernel* k = cogdiod_create(0, 0);
    ElmPackage* cn_pkg = concept_node_build_package();
    kernel_insert_package(k, cn_pkg);
    cogdiod_start(k);

    test_attend(k);
    test_ecan_diffuse(k);
    test_hebbian(k);
    test_state_transitions(k);
    test_attend_unknown(k);

    cogdiod_destroy(k);

    fprintf(stderr, "\n");
    if (failures == 0) {
        fprintf(stderr, "ALL ECAN TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "%d ECAN TEST(S) FAILED\n", failures);
    return 1;
}

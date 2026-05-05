/*
 * test_pln.c — Unit tests for PLN inference rules
 *
 * Compile: cc -O2 -std=c11 -Iinclude -D_GNU_SOURCE \
 *              -DDISVM_NREGS=16 -DDISVM_STKMAX=4096 \
 *              src/kernel/pln.c tests/test_pln.c -lm -lpthread -o test_pln
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "cogdiod.h"

/* ── Minimal stubs ────────────────────────────────────────────────────── */

static int pass = 0, fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { printf("PASS: %s\n", msg); pass++; } \
    else       { printf("FAIL: %s (line %d)\n", msg, __LINE__); fail++; } \
} while(0)

#define CHECK_NEAR(a, b, eps, msg) \
    CHECK(fabs((double)(a) - (double)(b)) < (eps), msg)

/* ── Test helpers ─────────────────────────────────────────────────────── */

static TruthValue tv(float s, float c) { return (TruthValue){s, c}; }

/* ── Tests ────────────────────────────────────────────────────────────── */

static void test_modus_ponens(void) {
    printf("\n[PLN] modus_ponens\n");
    TruthValue ant  = tv(0.8f, 0.9f);   /* P(A) */
    TruthValue impl = tv(0.95f, 0.8f);  /* P(B|A) */
    TruthValue res  = pln_modus_ponens(ant, impl);

    /* strength ≈ ant.s * impl.s */
    CHECK_NEAR(res.strength, 0.8f * 0.95f, 0.001f, "mp: strength = s_ant * s_impl");
    /* confidence should be less than either input */
    CHECK(res.confidence < ant.confidence, "mp: confidence < ant.confidence");
    CHECK(res.confidence < impl.confidence, "mp: confidence < impl.confidence");
    CHECK(res.confidence > 0, "mp: confidence > 0");
}

static void test_abduction(void) {
    printf("\n[PLN] abduction\n");
    TruthValue p_a  = tv(0.6f, 0.9f);  /* P(A) */
    TruthValue p_b  = tv(0.7f, 0.8f);  /* P(B) */
    TruthValue p_ab = tv(0.9f, 0.7f);  /* P(A|B) */
    TruthValue res  = pln_abduction(p_a, p_b, p_ab);

    CHECK(res.strength > 0.0f, "abd: strength > 0");
    CHECK(res.confidence > 0.0f, "abd: confidence > 0");
    CHECK(res.strength <= 1.0f, "abd: strength <= 1");
    CHECK(res.confidence <= 1.0f, "abd: confidence <= 1");
}

static void test_induction(void) {
    printf("\n[PLN] induction\n");
    TruthValue p_a  = tv(0.6f, 0.9f);  /* P(A) */
    TruthValue p_ba = tv(0.8f, 0.7f);  /* P(B|A) */
    TruthValue res  = pln_induction(p_a, p_ba);

    CHECK(res.strength > 0.0f, "ind: strength > 0");
    CHECK(res.confidence > 0.0f, "ind: confidence > 0");
    /* Induction is weaker than implication */
    CHECK(res.confidence < p_ba.confidence, "ind: confidence < impl confidence");
}

static void test_temporal_deduce(void) {
    printf("\n[PLN] temporal_deduce\n");
    TruthValue tv1 = tv(0.9f, 0.8f);   /* ab */
    TruthValue tv2 = tv(1.0f, 1.0f);   /* a  (full confidence so s = ab.s) */
    float decay = 0.95f;
    float steps = 3.0f;

    /* signature: pln_temporal_deduce(ab, a, steps, decay) */
    TruthValue res = pln_temporal_deduce(tv1, tv2, steps, decay);

    /* strength = ab.s * a.s */
    CHECK_NEAR(res.strength, tv1.strength * tv2.strength, 0.001f, "tmp: strength = ab.s * a.s");
    /* confidence decayed by decay^steps */
    float factor = decay * decay * decay;
    CHECK(res.confidence < tv1.confidence, "tmp: confidence decayed below ab.confidence");
    CHECK_NEAR(res.confidence, tv1.confidence * tv2.confidence * 0.9f * factor, 0.001f,
               "tmp: confidence = ab.c * a.c * 0.9 * decay^3");
}

static void test_boundary_conditions(void) {
    printf("\n[PLN] boundary conditions\n");
    TruthValue zero = tv(0.0f, 0.0f);
    TruthValue one  = tv(1.0f, 1.0f);

    TruthValue r1 = pln_modus_ponens(zero, one);
    CHECK_NEAR(r1.strength, 0.0f, 0.001f, "mp(0,1): strength = 0");

    TruthValue r2 = pln_modus_ponens(one, one);
    CHECK_NEAR(r2.strength, 1.0f, 0.001f, "mp(1,1): strength = 1");

    TruthValue r3 = pln_temporal_deduce(one, one, 0.0f, 1.0f);
    /* steps=0, decay=1.0 → decay^0=1, confidence = 1*1*0.9*1 = 0.9 */
    CHECK_NEAR(r3.confidence, 0.9f, 0.001f, "tmp(1,1,steps=0,decay=1): conf=0.9");
}

int main(void) {
    printf("=== PLN Unit Tests ===\n");
    test_modus_ponens();
    test_abduction();
    test_induction();
    test_temporal_deduce();
    test_boundary_conditions();
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}

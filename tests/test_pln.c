/*
 * test_pln.c — Unit tests for the PLN rule implementations in pln.c
 *
 * Tests all five PLN rules with known input/output values.
 * Run:  cc -O2 -std=c11 -Iinclude -o test_pln tests/test_pln.c \
 *          src/kernel/pln.c src/kernel/cogdiod_log.c -lm && ./test_pln
 */

#include "cogdiod.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static int failures = 0;

static int near(float a, float b, float eps) {
    return fabsf(a - b) < eps;
}

#define PASS(msg) fprintf(stderr, "PASS: %s\n", (msg))
#define FAIL(msg) do { \
    fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
    failures++; \
} while(0)
#define CHECK_NEAR(val, expected, eps, msg) \
    do { if (near((val),(expected),(eps))) PASS(msg); \
         else { fprintf(stderr, "FAIL: %s — got %.4f, expected %.4f (eps=%.4f, line %d)\n", \
                        (msg), (float)(val), (float)(expected), (float)(eps), __LINE__); \
                failures++; } } while(0)

/* ── Test 1: pln_deduce ─────────────────────────────────────────────── */

static void test_pln_deduce(void) {
    fprintf(stderr, "\n=== Test 1: pln_deduce ===\n");
    /* pln_deduce(P(B|A), P(A)) → P(B)
     * s_B = s_AB * s_A = 0.95 * 0.80 = 0.76
     * c_B = c_AB * c_A * 0.9 = 0.80 * 0.90 * 0.9 = 0.648
     */
    TruthValue ab = { 0.95f, 0.80f };
    TruthValue a  = { 0.80f, 0.90f };
    TruthValue r  = pln_deduce(ab, a);
    CHECK_NEAR(r.strength,   0.760f, 0.005f, "pln_deduce strength");
    CHECK_NEAR(r.confidence, 0.648f, 0.005f, "pln_deduce confidence");

    /* Edge case: zero antecedent strength */
    TruthValue zero = { 0.0f, 0.9f };
    TruthValue r2 = pln_deduce(ab, zero);
    CHECK_NEAR(r2.strength, 0.0f, 0.001f, "pln_deduce zero antecedent strength");
}

/* ── Test 2: pln_revise ─────────────────────────────────────────────── */

static void test_pln_revise(void) {
    fprintf(stderr, "\n=== Test 2: pln_revise ===\n");
    /* tv1 = (0.70, 0.60), tv2 = (0.80, 0.40)
     * total_c = 1.00
     * s = (0.70*0.60 + 0.80*0.40) / 1.00 = (0.42 + 0.32) / 1.00 = 0.74
     * c = 1.00 / (1.00 + 1.00) = 0.5
     */
    TruthValue tv1 = { 0.70f, 0.60f };
    TruthValue tv2 = { 0.80f, 0.40f };
    TruthValue r   = pln_revise(tv1, tv2);
    CHECK_NEAR(r.strength,   0.74f, 0.005f, "pln_revise strength");
    CHECK_NEAR(r.confidence, 0.50f, 0.005f, "pln_revise confidence");

    /* Edge case: both zero confidence */
    TruthValue zero_c1 = { 0.5f, 0.0f };
    TruthValue zero_c2 = { 0.9f, 0.0f };
    TruthValue r2 = pln_revise(zero_c1, zero_c2);
    CHECK_NEAR(r2.strength, 0.5f, 0.001f, "pln_revise zero confidence preserves tv1");
}

/* ── Test 3: pln_modus_ponens ────────────────────────────────────────── */

static void test_pln_modus_ponens(void) {
    fprintf(stderr, "\n=== Test 3: pln_modus_ponens ===\n");
    /* Same formula as pln_deduce: P(B) = P(A→B) * P(A) */
    TruthValue a  = { 0.80f, 0.90f };
    TruthValue ab = { 0.95f, 0.80f };
    TruthValue r  = pln_modus_ponens(a, ab);
    CHECK_NEAR(r.strength,   0.760f, 0.005f, "pln_modus_ponens strength");
    CHECK_NEAR(r.confidence, 0.648f, 0.005f, "pln_modus_ponens confidence");
}

/* ── Test 4: pln_abduction ───────────────────────────────────────────── */

static void test_pln_abduction(void) {
    fprintf(stderr, "\n=== Test 4: pln_abduction ===\n");
    /* P(B→A) ≈ P(A→B) * P(A) / P(B), clamped to [0,1]
     * a=(0.8,0.9), b=(0.7,0.8), ab=(0.9,0.85)
     * s = 0.9 * 0.8 / 0.7 = 1.028 → clamped to 1.0
     * c = 0.85 * 0.9 * 0.8 * 0.85 = 0.5202
     */
    TruthValue a  = { 0.80f, 0.90f };
    TruthValue b  = { 0.70f, 0.80f };
    TruthValue ab = { 0.90f, 0.85f };
    TruthValue r  = pln_abduction(a, b, ab);
    CHECK_NEAR(r.strength,   1.00f, 0.001f, "pln_abduction strength clamped at 1.0");
    /* confidence > 0 */
    if (r.confidence > 0.0f)
        PASS("pln_abduction confidence > 0");
    else
        FAIL("pln_abduction confidence should be > 0");

    /* Edge case: b.strength near zero → result (0,0) */
    TruthValue bzero = { 1e-10f, 0.5f };
    TruthValue r2 = pln_abduction(a, bzero, ab);
    CHECK_NEAR(r2.strength,   0.0f, 0.001f, "pln_abduction zero b strength");
    CHECK_NEAR(r2.confidence, 0.0f, 0.001f, "pln_abduction zero b confidence");
}

/* ── Test 5: pln_induction ───────────────────────────────────────────── */

static void test_pln_induction(void) {
    fprintf(stderr, "\n=== Test 5: pln_induction ===\n");
    /* s_AB = s_B / s_A = 0.60 / 0.80 = 0.75
     * c    = c_A * c_B * 0.8 = 0.90 * 0.70 * 0.8 = 0.504
     */
    TruthValue a = { 0.80f, 0.90f };
    TruthValue b = { 0.60f, 0.70f };
    TruthValue r = pln_induction(a, b);
    CHECK_NEAR(r.strength,   0.75f, 0.005f, "pln_induction strength");
    CHECK_NEAR(r.confidence, 0.504f, 0.005f, "pln_induction confidence");

    /* Edge case: s_B > s_A → result clamped to 1 */
    TruthValue a_small = { 0.3f, 0.8f };
    TruthValue b_large = { 0.9f, 0.7f };
    TruthValue r2 = pln_induction(a_small, b_large);
    CHECK_NEAR(r2.strength, 1.0f, 0.001f, "pln_induction strength clamped to 1");
}

/* ── Test 6: pln_temporal_deduce ─────────────────────────────────────── */

static void test_pln_temporal_deduce(void) {
    fprintf(stderr, "\n=== Test 6: pln_temporal_deduce ===\n");
    /* Base deduction result, then decayed by decay^steps */
    TruthValue ab = { 0.95f, 0.80f };
    TruthValue a  = { 0.80f, 0.90f };
    float decay   = 0.90f;
    float steps   = 3.0f;
    TruthValue r  = pln_temporal_deduce(ab, a, steps, decay);

    /* base confidence = 0.80 * 0.90 * 0.9 = 0.648
     * after decay^3   = 0.648 * 0.729 = 0.4724
     */
    CHECK_NEAR(r.strength,   0.760f, 0.005f, "pln_temporal_deduce strength unchanged");
    CHECK_NEAR(r.confidence, 0.472f, 0.010f, "pln_temporal_deduce confidence decayed");

    /* zero steps = no decay */
    TruthValue r2 = pln_temporal_deduce(ab, a, 0.0f, decay);
    CHECK_NEAR(r2.confidence, 0.648f, 0.005f, "pln_temporal_deduce zero steps = no decay");
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "=== PLN Unit Test Suite ===\n");
    test_pln_deduce();
    test_pln_revise();
    test_pln_modus_ponens();
    test_pln_abduction();
    test_pln_induction();
    test_pln_temporal_deduce();
    fprintf(stderr, "\n");
    if (failures == 0) {
        fprintf(stderr, "ALL PLN TESTS PASSED\n");
        return 0;
    } else {
        fprintf(stderr, "%d PLN TEST(S) FAILED\n", failures);
        return 1;
    }
}

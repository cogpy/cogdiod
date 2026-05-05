/*
 * pln.c — Probabilistic Logic Networks rule implementations
 *
 * Canonical home for all PLN inference functions declared in cogdiod.h.
 * Both elm_loader.c (via extern) and the public bridge/language bindings
 * link against these definitions.
 *
 * Phase 5 (Item 17) verified formulas:
 *   - Deduction (Modus Ponens)
 *   - Revision  (Bayesian update)
 *   - Abduction (P(A|B) from P(A), P(B), P(B|A))
 *   - Induction (P(B|A) from observations)
 *   - Temporal Deduction (with exponential decay)
 */

#include "cogdiod.h"
#include <math.h>

TruthValue pln_deduce(TruthValue ab, TruthValue a) {
    float s = ab.strength * a.strength;
    float c = ab.confidence * a.confidence * 0.9f;
    return (TruthValue){ s, c };
}

TruthValue pln_revise(TruthValue tv1, TruthValue tv2) {
    float total_c = tv1.confidence + tv2.confidence;
    if (total_c < 1e-6f) return tv1;
    float s = (tv1.strength * tv1.confidence +
               tv2.strength * tv2.confidence) / total_c;
    float c = total_c / (total_c + 1.0f);
    return (TruthValue){ s, c };
}

TruthValue pln_modus_ponens(TruthValue a, TruthValue a_implies_b) {
    return pln_deduce(a_implies_b, a);
}

TruthValue pln_abduction(TruthValue a, TruthValue b, TruthValue ab) {
    if (b.strength < 1e-6f) return (TruthValue){0.0f, 0.0f};
    float s = ab.strength * a.strength / b.strength;
    if (s > 1.0f) s = 1.0f;
    float c = ab.confidence * a.confidence * b.confidence * 0.85f;
    return (TruthValue){s, c};
}

TruthValue pln_induction(TruthValue a, TruthValue b) {
    float s = (a.strength > 1e-6f) ? b.strength / a.strength : 0.0f;
    if (s > 1.0f) s = 1.0f;
    float c = a.confidence * b.confidence * 0.8f;
    return (TruthValue){s, c};
}

TruthValue pln_temporal_deduce(TruthValue ab, TruthValue a,
                                float time_steps, float decay) {
    TruthValue base = pln_deduce(ab, a);
    float factor = powf(decay, time_steps);
    base.confidence *= factor;
    return base;
}

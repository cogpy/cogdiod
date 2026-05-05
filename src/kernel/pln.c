/*
 * pln.c — Probabilistic Logic Networks inference rules for CogDiod
 */

#include "cogdiod.h"
#include <math.h>

TruthValue pln_modus_ponens(TruthValue a, TruthValue ab) {
    float s = ab.strength * a.strength;
    float c = ab.confidence * a.confidence * 0.9f;
    return (TruthValue){s, c};
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
                               float steps, float decay) {
    float s = ab.strength * a.strength;
    float c = ab.confidence * a.confidence * 0.9f * powf(decay, steps);
    return (TruthValue){s, c};
}

/*
 * pln.c — Probabilistic Logic Networks rule implementations
 *
 * These are the "public" PLN API functions declared in cogdiod.h.
 * The static versions in elm_loader.c handle opcode dispatch;
 * these are callable from C (e.g., the bridge and language bindings).
 */

#include "cogdiod.h"
#include <math.h>

/* All implementations delegate to the versions in elm_loader.c via
 * the PLN rule functions declared in cogdiod.h.  We provide them
 * here as extern definitions so external translation units can link
 * against them without pulling in the full elm_loader object.     */

TruthValue pln_modus_ponens(TruthValue a, TruthValue a_implies_b) {
    float s = a_implies_b.strength * a.strength;
    float c = a_implies_b.confidence * a.confidence * 0.9f;
    return (TruthValue){ s, c };
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
    float s = ab.strength * a.strength;
    float c = ab.confidence * a.confidence * 0.9f;
    float factor = powf(decay, time_steps);
    c *= factor;
    return (TruthValue){ s, c };
}
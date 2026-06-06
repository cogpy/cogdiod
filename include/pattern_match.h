/*
 * pattern_match.h — Pattern matching engine types and API (Phase 3)
 */

#pragma once

#include "cogdiod.h"
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Binding structures
 * ───────────────────────────────────────────────────────────────────────── */

#define MAX_BINDINGS 64

typedef struct {
    uint64_t var_uuid;      /* UUID of the VariableNode */
    uint64_t bound_uuid;    /* UUID of the atom it's bound to */
} Binding;

typedef struct {
    Binding  entries[MAX_BINDINGS];
    int      count;
} Bindings;

/* ─────────────────────────────────────────────────────────────────────────
 * Binding operations
 * ───────────────────────────────────────────────────────────────────────── */

void     bindings_init(Bindings* b);
int      bindings_add(Bindings* b, uint64_t var, uint64_t val);
uint64_t bindings_lookup(Bindings* b, uint64_t var);
int      bindings_to_json(Bindings* b, char* buf, size_t max);

/* ─────────────────────────────────────────────────────────────────────────
 * Unification and pattern matching
 * ───────────────────────────────────────────────────────────────────────── */

typedef void (*MatchCallback)(uint64_t matched_uuid, Bindings* bindings, void* ctx);

int unify(CogDiodKernel* k,
          AtomIsolate* pattern,
          AtomIsolate* graph,
          Bindings* out);

int cogdiod_match(CogDiodKernel* k,
                  uint64_t bind_link_uuid,
                  MatchCallback cb,
                  void* ctx);

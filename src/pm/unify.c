/*
 * unify.c — Unification engine for pattern matching (Phase 3.1)
 *
 * Implements Prolog-style unification with variable binding:
 *   - VariableNode wildcards
 *   - Typed variables
 *   - Deep link traversal
 *
 * Returns a binding list {variable_uuid → matched_uuid}
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
 * Type ID constants (djb2 hashes)
 * ───────────────────────────────────────────────────────────────────────── */

static uint32_t variable_node_type_id(void) {
    static uint32_t id = 0;
    if (id == 0) id = cogdiod_hash_type("VariableNode");
    return id;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Binding operations
 * ───────────────────────────────────────────────────────────────────────── */

void bindings_init(Bindings* b) {
    b->count = 0;
}

int bindings_add(Bindings* b, uint64_t var, uint64_t val) {
    if (b->count >= MAX_BINDINGS) return -1;
    
    /* Check if already bound */
    for (int i = 0; i < b->count; i++) {
        if (b->entries[i].var_uuid == var) {
            /* Already bound - check consistency */
            return (b->entries[i].bound_uuid == val) ? 0 : -1;
        }
    }
    
    b->entries[b->count].var_uuid   = var;
    b->entries[b->count].bound_uuid = val;
    b->count++;
    return 0;
}

uint64_t bindings_lookup(Bindings* b, uint64_t var) {
    for (int i = 0; i < b->count; i++) {
        if (b->entries[i].var_uuid == var) {
            return b->entries[i].bound_uuid;
        }
    }
    return 0; /* Not bound */
}

/* ─────────────────────────────────────────────────────────────────────────
 * Unification
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * unify — Attempt to unify a pattern atom with a graph atom
 *
 * Returns 0 on success (atoms unify), -1 on failure
 * Populates 'out' with variable bindings
 *
 * Rules:
 *   1. VariableNode unifies with any atom, creating a binding
 *   2. Same-type nodes with matching names unify
 *   3. Links unify if all outgoing channels unify (recursive)
 */
int unify(CogDiodKernel* k,
          AtomIsolate* pattern,
          AtomIsolate* graph,
          Bindings* out) {
    
    if (!pattern || !graph) return -1;
    
    /* Rule 1: VariableNode matches anything */
    if (pattern->type_id == variable_node_type_id()) {
        /* Check if already bound */
        uint64_t existing = bindings_lookup(out, pattern->uuid);
        if (existing != 0) {
            /* Must match the same atom */
            return (existing == graph->uuid) ? 0 : -1;
        }
        /* Create new binding */
        return bindings_add(out, pattern->uuid, graph->uuid);
    }
    
    /* Rule 2: Types must match */
    if (pattern->type_id != graph->type_id) {
        return -1;
    }
    
    /* Rule 2b: Names must match (for nodes) */
    if (pattern->outgoing_count == 0 && graph->outgoing_count == 0) {
        /* Both are nodes - compare names */
        if (strlen(pattern->name) > 0 && strlen(graph->name) > 0) {
            if (strcmp(pattern->name, graph->name) != 0) {
                return -1;
            }
        }
        return 0; /* Names match or one is anonymous */
    }
    
    /* Rule 3: Links must have same arity */
    if (pattern->outgoing_count != graph->outgoing_count) {
        return -1;
    }
    
    /* Rule 3b: Recursively unify all outgoing targets */
    LimboChannel* pch = pattern->outgoing;
    LimboChannel* gch = graph->outgoing;
    
    while (pch && gch) {
        AtomIsolate* p_target = cogdiod_get_atom(k, pch->dst_uuid);
        AtomIsolate* g_target = cogdiod_get_atom(k, gch->dst_uuid);
        
        if (unify(k, p_target, g_target, out) != 0) {
            return -1;
        }
        
        pch = pch->out_next;
        gch = gch->out_next;
    }
    
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Pattern match interface
 * ───────────────────────────────────────────────────────────────────────── */

typedef void (*MatchCallback)(uint64_t matched_uuid, Bindings* bindings, void* ctx);

/*
 * cogdiod_match — Execute pattern matching from a BindLink
 *
 * Traverses all atoms reachable from the kernel's pool and calls
 * the callback for each binding satisfying the BindLink pattern.
 *
 * BindLink structure (outgoing):
 *   [0] = variables list (ListLink of VariableNodes)
 *   [1] = pattern body (the pattern to match)
 */
int cogdiod_match(CogDiodKernel* k,
                  uint64_t bind_link_uuid,
                  MatchCallback cb,
                  void* ctx) {
    AtomIsolate* bind = cogdiod_get_atom(k, bind_link_uuid);
    if (!bind) return -1;
    
    /* Get the pattern body (second outgoing) */
    LimboChannel* ch = bind->outgoing;
    if (!ch || !ch->out_next) return -1;
    ch = ch->out_next; /* Skip variables list */
    
    AtomIsolate* pattern = cogdiod_get_atom(k, ch->dst_uuid);
    if (!pattern) return -1;
    
    int match_count = 0;
    
    /* Traverse all atoms and try to unify with pattern */
    pthread_rwlock_rdlock(&k->pool_lock);
    for (int i = 0; i < ATOM_POOL_BUCKETS; i++) {
        AtomIsolate* a = k->atom_pool[i];
        while (a) {
            if (a->uuid != bind_link_uuid && a->state == ATOM_ALIVE) {
                Bindings bindings;
                bindings_init(&bindings);
                
                if (unify(k, pattern, a, &bindings) == 0) {
                    match_count++;
                    if (cb) {
                        cb(a->uuid, &bindings, ctx);
                    }
                }
            }
            a = a->ht_next;
        }
    }
    pthread_rwlock_unlock(&k->pool_lock);
    
    return match_count;
}

/* ─────────────────────────────────────────────────────────────────────────
 * JSON serialization for bindings
 * ───────────────────────────────────────────────────────────────────────── */

int bindings_to_json(Bindings* b, char* buf, size_t max) {
    int pos = 0;
    int n;
    
    n = snprintf(buf + pos, max - (size_t)pos, "[");
    if (n < 0 || (size_t)n >= max - (size_t)pos) return -1;
    pos += n;
    
    for (int i = 0; i < b->count; i++) {
        if (i > 0) {
            n = snprintf(buf + pos, max - (size_t)pos, ",");
            if (n < 0 || (size_t)n >= max - (size_t)pos) return -1;
            pos += n;
        }
        n = snprintf(buf + pos, max - (size_t)pos,
                     "{\"var\":%llu,\"val\":%llu}",
                     (unsigned long long)b->entries[i].var_uuid,
                     (unsigned long long)b->entries[i].bound_uuid);
        if (n < 0 || (size_t)n >= max - (size_t)pos) return -1;
        pos += n;
    }
    
    n = snprintf(buf + pos, max - (size_t)pos, "]");
    if (n < 0 || (size_t)n >= max - (size_t)pos) return -1;
    pos += n;
    
    return pos;
}

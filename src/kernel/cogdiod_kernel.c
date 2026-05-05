/*
 * cogdiod_kernel.c — CogDiod micro-kernel implementation
 *
 * Manages the lifecycle of AtomIsolates, ElmPackages, and LimboChannels.
 * The kernel is the root of the CogDiod daemon; it owns the 9P server,
 * the isolate pool, and the package cache.
 */

#include "cogdiod.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

/* Forward declaration for worker / ecan */
static void* worker_thread(void* arg);
static void* ecan_thread_fn(void* arg);

/* ─────────────────────────────────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────────────────────────────────── */

uint32_t cogdiod_hash_type(const char* name) {
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*name++))
        h = ((h << 5) + h) ^ c;
    return h;
}

uint64_t cogdiod_next_uuid(CogDiodKernel* k) {
    return __sync_fetch_and_add(&k->next_uuid, 1);
}

static uint32_t pool_bucket(uint64_t uuid) {
    return (uint32_t)(uuid % ATOM_POOL_BUCKETS);
}

static uint32_t pkg_bucket(uint32_t type_id) {
    return type_id % PKG_CACHE_BUCKETS;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Kernel lifecycle
 * ───────────────────────────────────────────────────────────────────────── */

CogDiodKernel* cogdiod_create(uint16_t port, uint32_t workers) {
    CogDiodKernel* k = calloc(1, sizeof(CogDiodKernel));
    if (!k) return NULL;

    k->server.port    = port ? port : DISTYX_PORT_DEFAULT;
    k->server.msize   = DISTYX_MSIZE;
    k->server.running = false;

    k->worker_count   = workers ? workers : 4;
    k->workers        = calloc(k->worker_count, sizeof(pthread_t));
    k->next_uuid      = 1;
    k->sti_funds      = 1000.0f;
    k->running        = false;
    k->ecan_enabled   = false;

    /* Run queue: circular buffer sized to 8x worker count */
    k->rq_cap    = (k->worker_count * 8) + 1;
    k->run_queue = calloc(k->rq_cap, sizeof(AtomIsolate*));
    k->rq_head   = 0;
    k->rq_tail   = 0;

    pthread_rwlock_init(&k->pool_lock, NULL);
    pthread_mutex_init(&k->pkg_lock, NULL);
    pthread_mutex_init(&k->sti_lock, NULL);
    pthread_mutex_init(&k->run_queue_lock, NULL);
    pthread_cond_init(&k->run_queue_cond, NULL);

    memset(k->atom_pool, 0, sizeof(k->atom_pool));
    memset(k->pkg_cache, 0, sizeof(k->pkg_cache));

    fprintf(stderr, "[cogdiod] kernel created (port=%u workers=%u)\n",
            k->server.port, k->worker_count);
    return k;
}

void cogdiod_destroy(CogDiodKernel* k) {
    if (!k) return;
    cogdiod_stop(k);

    /* Free all remaining atoms */
    pthread_rwlock_wrlock(&k->pool_lock);
    for (int i = 0; i < ATOM_POOL_BUCKETS; i++) {
        AtomIsolate* a = k->atom_pool[i];
        while (a) {
            AtomIsolate* next = a->ht_next;
            free(a->vm_ctx.stack);
            free(a->vm_ctx.heap);
            free(a);
            a = next;
        }
    }
    pthread_rwlock_unlock(&k->pool_lock);

    /* Free all packages (walk collision chains) */
    pthread_mutex_lock(&k->pkg_lock);
    for (int i = 0; i < PKG_CACHE_BUCKETS; i++) {
        ElmPackage* p = k->pkg_cache[i];
        while (p) {
            ElmPackage* next = p->next_in_cache;
            free(p->dis_bytecode);
            pthread_mutex_destroy(&p->ref_lock);
            free(p);
            p = next;
        }
        k->pkg_cache[i] = NULL;
    }
    pthread_mutex_unlock(&k->pkg_lock);

    free(k->workers);
    free(k->run_queue);
    pthread_rwlock_destroy(&k->pool_lock);
    pthread_mutex_destroy(&k->pkg_lock);
    pthread_mutex_destroy(&k->sti_lock);
    pthread_mutex_destroy(&k->run_queue_lock);
    pthread_cond_destroy(&k->run_queue_cond);
    free(k);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Package management
 * ───────────────────────────────────────────────────────────────────────── */

ElmPackage* cogdiod_load_package(CogDiodKernel* k, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[cogdiod] cannot open package: %s\n", path);
        return NULL;
    }

    /* Read header */
    ElmPackage hdr;
    if (fread(&hdr, sizeof(ElmPackage) - sizeof(uint8_t*), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (hdr.magic != ELM_MAGIC) {
        fprintf(stderr, "[cogdiod] bad magic in %s\n", path);
        fclose(f);
        return NULL;
    }

    /* Read bytecode */
    uint8_t* bc = malloc(hdr.bytecode_size);
    if (!bc || fread(bc, 1, hdr.bytecode_size, f) != hdr.bytecode_size) {
        free(bc);
        fclose(f);
        return NULL;
    }
    fclose(f);

    ElmPackage* pkg = calloc(1, sizeof(ElmPackage));
    *pkg = hdr;
    pkg->dis_bytecode  = bc;
    pkg->ref_count     = 0;
    pkg->next_in_cache = NULL;
    pthread_mutex_init(&pkg->ref_lock, NULL);

    /* Insert into cache with collision chaining */
    pthread_mutex_lock(&k->pkg_lock);
    uint32_t b = pkg_bucket(pkg->type_id);
    pkg->next_in_cache = k->pkg_cache[b];
    k->pkg_cache[b] = pkg;
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);

    fprintf(stderr, "[cogdiod] loaded package '%s' (type_id=0x%08x, %zu bytes)\n",
            pkg->name, pkg->type_id, pkg->bytecode_size);
    return pkg;
}

ElmPackage* cogdiod_get_package(CogDiodKernel* k, uint32_t type_id) {
    pthread_mutex_lock(&k->pkg_lock);
    ElmPackage* p = k->pkg_cache[pkg_bucket(type_id)];
    while (p && p->type_id != type_id)
        p = p->next_in_cache;
    pthread_mutex_unlock(&k->pkg_lock);
    return p;
}

void cogdiod_unload_package(CogDiodKernel* k, uint32_t type_id) {
    pthread_mutex_lock(&k->pkg_lock);
    uint32_t b = pkg_bucket(type_id);
    ElmPackage** pp = &k->pkg_cache[b];

    while (*pp && (*pp)->type_id != type_id)
        pp = &(*pp)->next_in_cache;

    if (!*pp) {
        pthread_mutex_unlock(&k->pkg_lock);
        return;
    }

    ElmPackage* p = *pp;

    /* Refuse to unload while active isolates still hold references */
    pthread_mutex_lock(&p->ref_lock);
    uint32_t refs = p->ref_count;
    pthread_mutex_unlock(&p->ref_lock);

    if (refs > 0) {
        pthread_mutex_unlock(&k->pkg_lock);
        fprintf(stderr, "[cogdiod] warning: cannot unload package "
                "type_id=0x%08x — %u active ref(s)\n", type_id, refs);
        return;
    }

    /* Unlink from chain */
    *pp = p->next_in_cache;
    if (k->pkg_count > 0)
        k->pkg_count--;
    pthread_mutex_unlock(&k->pkg_lock);

    pthread_mutex_destroy(&p->ref_lock);
    free(p->dis_bytecode);
    free(p);
    fprintf(stderr, "[cogdiod] unloaded package type_id=0x%08x\n", type_id);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Dis VM isolate initialisation
 * ───────────────────────────────────────────────────────────────────────── */

static int disvm_init(DisVMContext* ctx, ElmPackage* pkg) {
    ctx->pc      = pkg->ep_init;
    ctx->sp      = 0;
    ctx->running = false;
    ctx->kernel_ref = NULL;
    ctx->stack   = calloc(DISVM_STKMAX, sizeof(uint8_t));
    ctx->heap    = calloc(4096, sizeof(uint8_t));
    ctx->heap_size = 4096;
    if (!ctx->stack || !ctx->heap) return -1;
    memset(ctx->regs, 0, sizeof(ctx->regs));
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Atom lifecycle
 * ───────────────────────────────────────────────────────────────────────── */

AtomIsolate* cogdiod_spawn(CogDiodKernel* k,
                           const char* type_name,
                           const char* atom_name) {
    uint32_t type_id = cogdiod_hash_type(type_name);
    ElmPackage* pkg  = cogdiod_get_package(k, type_id);

    if (!pkg) {
        fprintf(stderr, "[cogdiod] no package for type '%s' (0x%08x)\n",
                type_name, type_id);
        return NULL;
    }

    AtomIsolate* a = calloc(1, sizeof(AtomIsolate));
    if (!a) return NULL;

    a->uuid    = cogdiod_next_uuid(k);
    a->type_id = type_id;
    a->state   = ATOM_SLEEPING;
    a->package = pkg;
    a->history_count = 0;
    a->hebbian_weight = 1.0f;

    if (atom_name)
        strncpy(a->name, atom_name, ATOM_NAME_MAX - 1);

    /* Default TruthValue and AttentionValue */
    a->tv  = (TruthValue){0.5f, 0.1f};
    a->av  = (AttentionValue){0.0f, 0.0f};

    /* Initialise the Dis VM context */
    if (disvm_init(&a->vm_ctx, pkg) != 0) {
        free(a);
        return NULL;
    }
    a->vm_ctx.kernel_ref = k;

    /* Increment package reference count */
    pthread_mutex_lock(&pkg->ref_lock);
    pkg->ref_count++;
    pthread_mutex_unlock(&pkg->ref_lock);

    pthread_mutex_init(&a->lock, NULL);

    /* Insert into pool */
    pthread_rwlock_wrlock(&k->pool_lock);
    uint32_t b    = pool_bucket(a->uuid);
    a->ht_next    = k->atom_pool[b];
    k->atom_pool[b] = a;
    k->atom_count++;
    pthread_rwlock_unlock(&k->pool_lock);

    fprintf(stderr, "[cogdiod] spawned %s '%s' uuid=%llu\n",
            type_name, atom_name ? atom_name : "(anon)",
            (unsigned long long)a->uuid);
    return a;
}

AtomIsolate* cogdiod_get_atom(CogDiodKernel* k, uint64_t uuid) {
    pthread_rwlock_rdlock(&k->pool_lock);
    AtomIsolate* a = k->atom_pool[pool_bucket(uuid)];
    while (a && a->uuid != uuid) a = a->ht_next;
    pthread_rwlock_unlock(&k->pool_lock);
    return a;
}

/* Lock-free fast path using atomic load */
AtomIsolate* cogdiod_get_atom_fast(CogDiodKernel* k, uint64_t uuid) {
    AtomIsolate* head = __atomic_load_n(
        &k->atom_pool[pool_bucket(uuid)], __ATOMIC_ACQUIRE);
    while (head && head->uuid != uuid)
        head = __atomic_load_n(&head->ht_next, __ATOMIC_ACQUIRE);
    return head;
}

int cogdiod_destroy_atom(CogDiodKernel* k, uint64_t uuid) {
    pthread_rwlock_wrlock(&k->pool_lock);
    uint32_t b    = pool_bucket(uuid);
    AtomIsolate** pp = &k->atom_pool[b];
    while (*pp && (*pp)->uuid != uuid) pp = &(*pp)->ht_next;
    if (!*pp) { pthread_rwlock_unlock(&k->pool_lock); return -1; }

    AtomIsolate* a = *pp;
    *pp = a->ht_next;
    k->atom_count--;
    pthread_rwlock_unlock(&k->pool_lock);

    a->state = ATOM_DYING;

    /* Decrement package ref count */
    pthread_mutex_lock(&a->package->ref_lock);
    a->package->ref_count--;
    pthread_mutex_unlock(&a->package->ref_lock);

    free(a->vm_ctx.stack);
    free(a->vm_ctx.heap);
    pthread_mutex_destroy(&a->lock);
    free(a);

    fprintf(stderr, "[cogdiod] destroyed atom uuid=%llu\n",
            (unsigned long long)uuid);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Channel operations
 * ───────────────────────────────────────────────────────────────────────── */

LimboChannel* cogdiod_link(CogDiodKernel* k,
                           uint64_t src_uuid, uint64_t dst_uuid) {
    AtomIsolate* src = cogdiod_get_atom(k, src_uuid);
    AtomIsolate* dst = cogdiod_get_atom(k, dst_uuid);
    if (!src || !dst) return NULL;

    LimboChannel* ch = calloc(1, sizeof(LimboChannel));
    if (!ch) return NULL;

    ch->src_uuid  = src_uuid;
    ch->dst_uuid  = dst_uuid;
    ch->head      = 0;
    ch->tail      = 0;
    ch->ref_count = 2;   /* one ref per endpoint list */
    ch->weight    = 1.0f;
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);

    /* Add to src->outgoing via out_next */
    pthread_mutex_lock(&src->lock);
    ch->out_next   = src->outgoing;
    src->outgoing  = ch;
    src->outgoing_count++;
    pthread_mutex_unlock(&src->lock);

    /* Add to dst->incoming via in_next (same channel object) */
    pthread_mutex_lock(&dst->lock);
    ch->in_next    = dst->incoming;
    dst->incoming  = ch;
    dst->incoming_count++;
    pthread_mutex_unlock(&dst->lock);

    fprintf(stderr, "[cogdiod] linked %llu -> %llu\n",
            (unsigned long long)src_uuid,
            (unsigned long long)dst_uuid);
    return ch;
}

int cogdiod_send(LimboChannel* ch, const CogMessage* msg) {
    pthread_mutex_lock(&ch->lock);
    uint32_t next = (ch->tail + 1) % CHANNEL_BUF_MAX;
    while (next == ch->head)   /* buffer full: block */
        pthread_cond_wait(&ch->not_full, &ch->lock);
    ch->buf[ch->tail] = *msg;
    ch->tail = next;
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int cogdiod_recv(LimboChannel* ch, CogMessage* msg) {
    pthread_mutex_lock(&ch->lock);
    while (ch->head == ch->tail)  /* buffer empty: block */
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    *msg = ch->buf[ch->head];
    ch->head = (ch->head + 1) % CHANNEL_BUF_MAX;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

/* Non-blocking receive: returns 1 if message received, 0 if empty */
static int cogdiod_try_recv(LimboChannel* ch, CogMessage* msg) {
    pthread_mutex_lock(&ch->lock);
    if (ch->head == ch->tail) {
        pthread_mutex_unlock(&ch->lock);
        return 0;
    }
    *msg = ch->buf[ch->head];
    ch->head = (ch->head + 1) % CHANNEL_BUF_MAX;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
    return 1;
}

static void free_channel(LimboChannel* ch) {
    pthread_mutex_destroy(&ch->lock);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch);
}

int cogdiod_unlink(CogDiodKernel* k,
                   uint64_t src_uuid, uint64_t dst_uuid) {
    AtomIsolate* src = cogdiod_get_atom(k, src_uuid);
    AtomIsolate* dst = cogdiod_get_atom(k, dst_uuid);
    if (!src || !dst) return -1;

    LimboChannel* found_ch = NULL;

    /* Step 1: Remove from src->outgoing (follow out_next) */
    pthread_mutex_lock(&src->lock);
    LimboChannel** pp = &src->outgoing;
    while (*pp) {
        if ((*pp)->dst_uuid == dst_uuid) {
            found_ch = *pp;
            *pp = found_ch->out_next;
            src->outgoing_count--;
            break;
        }
        pp = &(*pp)->out_next;
    }
    pthread_mutex_unlock(&src->lock);

    if (!found_ch) return -1;

    /* Step 2: Remove from dst->incoming (follow in_next) */
    pthread_mutex_lock(&dst->lock);
    pp = &dst->incoming;
    while (*pp) {
        if (*pp == found_ch) {
            *pp = found_ch->in_next;
            dst->incoming_count--;
            break;
        }
        pp = &(*pp)->in_next;
    }
    pthread_mutex_unlock(&dst->lock);

    /* Decrement ref_count by 2 (removed from both lists) and free */
    uint32_t new_rc = __sync_sub_and_fetch(&found_ch->ref_count, 2);
    if (new_rc == 0)
        free_channel(found_ch);

    fprintf(stderr, "[cogdiod] unlinked %llu -> %llu\n",
            (unsigned long long)src_uuid,
            (unsigned long long)dst_uuid);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Worker thread pool
 * ───────────────────────────────────────────────────────────────────────── */

void cogdiod_enqueue(CogDiodKernel* k, AtomIsolate* a) {
    pthread_mutex_lock(&k->run_queue_lock);
    uint32_t next_tail = (k->rq_tail + 1) % k->rq_cap;
    if (next_tail != k->rq_head) {   /* drop if full */
        k->run_queue[k->rq_tail] = a;
        k->rq_tail = next_tail;
        pthread_cond_signal(&k->run_queue_cond);
    }
    pthread_mutex_unlock(&k->run_queue_lock);
}

/* Forward declaration from elm_loader.c */
int elm_exec_msg(AtomIsolate* a, const CogMessage* msg);

static void* worker_thread(void* arg) {
    CogDiodKernel* k = (CogDiodKernel*)arg;

    while (1) {
        pthread_mutex_lock(&k->run_queue_lock);
        while (k->rq_head == k->rq_tail && k->running)
            pthread_cond_wait(&k->run_queue_cond, &k->run_queue_lock);

        if (!k->running && k->rq_head == k->rq_tail) {
            pthread_mutex_unlock(&k->run_queue_lock);
            break;
        }

        AtomIsolate* a = k->run_queue[k->rq_head];
        k->rq_head = (k->rq_head + 1) % k->rq_cap;
        pthread_mutex_unlock(&k->run_queue_lock);

        if (!a || a->state == ATOM_DYING) continue;

        /* Drain one message from each incoming channel */
        bool has_more = false;
        LimboChannel* ch = a->incoming;
        while (ch) {
            CogMessage msg;
            if (cogdiod_try_recv(ch, &msg)) {
                elm_exec_msg(a, &msg);
                /* Check if more messages remain */
                pthread_mutex_lock(&ch->lock);
                if (ch->head != ch->tail) has_more = true;
                pthread_mutex_unlock(&ch->lock);
            }
            ch = ch->in_next;
        }

        /* Re-enqueue if more messages pending */
        if (has_more)
            cogdiod_enqueue(k, a);
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ECAN background thread
 * ───────────────────────────────────────────────────────────────────────── */

void cogdiod_ecan_diffuse(CogDiodKernel* k) {
    pthread_rwlock_rdlock(&k->pool_lock);
    for (int i = 0; i < ATOM_POOL_BUCKETS; i++) {
        AtomIsolate* a = k->atom_pool[i];
        while (a) {
            pthread_mutex_lock(&a->lock);

            /* LTI slow decay */
            a->av.lti *= 0.999f;

            /* STI spread to outgoing neighbours */
            if (a->av.sti > 1.0f) {
                float spread = a->av.sti * 0.3f;
                LimboChannel* ch = a->outgoing;
                uint32_t n = a->outgoing_count;
                if (n > 0) {
                    float per_ch = spread / (float)n;
                    a->av.sti -= spread;
                    while (ch) {
                        CogMessage msg = {
                            .type        = MSG_ATTEND,
                            .sender_uuid = a->uuid,
                            .av          = { per_ch, 0.0f },
                        };
                        /* Non-blocking send: skip if full */
                        pthread_mutex_lock(&ch->lock);
                        uint32_t nxt = (ch->tail + 1) % CHANNEL_BUF_MAX;
                        if (nxt != ch->head) {
                            ch->buf[ch->tail] = msg;
                            ch->tail = nxt;
                            pthread_cond_signal(&ch->not_empty);
                        }
                        pthread_mutex_unlock(&ch->lock);
                        ch = ch->out_next;
                    }
                }
            }

            pthread_mutex_unlock(&a->lock);
            a = a->ht_next;
        }
    }
    pthread_rwlock_unlock(&k->pool_lock);
}

static void* ecan_thread_fn(void* arg) {
    CogDiodKernel* k = (CogDiodKernel*)arg;
    while (k->running && k->ecan_enabled) {
        cogdiod_ecan_diffuse(k);
        usleep(100000);   /* 100 ms between passes */
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Hebbian learning
 * ───────────────────────────────────────────────────────────────────────── */

void cogdiod_hebbian_update(CogDiodKernel* k,
                            uint64_t src_uuid, uint64_t dst_uuid) {
    AtomIsolate* src = cogdiod_get_atom(k, src_uuid);
    AtomIsolate* dst = cogdiod_get_atom(k, dst_uuid);
    if (!src || !dst) return;

    /* Find the channel between src and dst */
    pthread_mutex_lock(&src->lock);
    LimboChannel* ch = src->outgoing;
    while (ch && ch->dst_uuid != dst_uuid)
        ch = ch->out_next;
    if (ch) {
        /* Hebbian rule: strengthen if both atoms are active */
        float src_act = src->av.sti > 0.0f ? src->av.sti : 0.0f;
        float dst_act = dst->av.sti > 0.0f ? dst->av.sti : 0.0f;
        ch->weight += 0.01f * src_act * dst_act;
        if (ch->weight > 10.0f) ch->weight = 10.0f;
    }
    pthread_mutex_unlock(&src->lock);

    /* Update atom-level hebbian weight */
    pthread_mutex_lock(&dst->lock);
    dst->hebbian_weight += 0.005f * src->av.sti;
    if (dst->hebbian_weight > 10.0f) dst->hebbian_weight = 10.0f;
    pthread_mutex_unlock(&dst->lock);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cognitive operations
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_set_tv(CogDiodKernel* k, uint64_t uuid, TruthValue tv) {
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    if (!a) return -1;

    pthread_mutex_lock(&a->lock);

    /* Push to episodic history */
    int hi = a->history_count % 8;
    a->tv_history[hi][0] = a->tv.strength;
    a->tv_history[hi][1] = a->tv.confidence;
    a->history_count++;

    a->tv = tv;
    pthread_mutex_unlock(&a->lock);

    /* Notify all outgoing channels that our TV changed */
    CogMessage msg = {
        .type        = MSG_SOURCE_CHANGED,
        .sender_uuid = uuid,
        .tv          = tv,
    };
    LimboChannel* ch = a->outgoing;
    while (ch) {
        cogdiod_send(ch, &msg);
        /* Wake the destination atom's worker */
        AtomIsolate* dst = cogdiod_get_atom_fast(k, ch->dst_uuid);
        if (dst) cogdiod_enqueue(k, dst);
        ch = ch->out_next;
    }
    return 0;
}

TruthValue cogdiod_get_tv(CogDiodKernel* k, uint64_t uuid) {
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    if (!a) return (TruthValue){0.0f, 0.0f};
    return a->tv;
}

int cogdiod_attend(CogDiodKernel* k, uint64_t uuid, float sti_delta) {
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    if (!a) return -1;

    pthread_mutex_lock(&a->lock);
    a->av.sti += sti_delta;
    if (a->av.sti > 0.0f) a->state = ATOM_ALIVE;
    pthread_mutex_unlock(&a->lock);

    pthread_mutex_lock(&k->sti_lock);
    k->total_sti += sti_delta;
    pthread_mutex_unlock(&k->sti_lock);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Kernel start / stop
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_start(CogDiodKernel* k) {
    k->running = true;

    /* Launch worker threads */
    for (uint32_t i = 0; i < k->worker_count; i++)
        pthread_create(&k->workers[i], NULL, worker_thread, k);

    /* Launch ECAN thread */
    k->ecan_enabled = true;
    pthread_create(&k->ecan_thread, NULL, ecan_thread_fn, k);

    fprintf(stderr, "[cogdiod] kernel started\n");
    return 0;
}

void cogdiod_stop(CogDiodKernel* k) {
    if (!k->running) return;
    k->running      = false;
    k->ecan_enabled = false;

    /* Wake all workers so they can exit */
    pthread_mutex_lock(&k->run_queue_lock);
    pthread_cond_broadcast(&k->run_queue_cond);
    pthread_mutex_unlock(&k->run_queue_lock);

    for (uint32_t i = 0; i < k->worker_count; i++)
        pthread_join(k->workers[i], NULL);

    pthread_join(k->ecan_thread, NULL);

    fprintf(stderr, "[cogdiod] kernel stopped\n");
}

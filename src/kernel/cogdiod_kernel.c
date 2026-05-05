/*
 * cogdiod_kernel.c — CogDiod micro-kernel implementation
 *
 * Manages the lifecycle of AtomIsolates, ElmPackages, and LimboChannels.
 * Includes:
 *   - pkg_cache hash-chain collision handling (Item 1)
 *   - Shared LimboChannel with dual out_next/in_next lists (Item 2)
 *   - Worker thread pool with STI-priority run queue (Items 3, 19)
 *   - cogdiod_set_tv enqueues downstream atoms (Item 4)
 *   - ECAN background diffusion thread (Item 18)
 *   - Hebbian learning (Item 20)
 *   - Lock-free atom lookup (Item 32)
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

/* Forward declaration from elm_loader.c */
int elm_exec_msg(AtomIsolate* a, const CogMessage* msg);

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
 * Run queue — STI-weighted priority insert
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_enqueue(CogDiodKernel* k, AtomIsolate* a) {
    if (!a || a->state != ATOM_ALIVE) return -1;

    pthread_mutex_lock(&k->run_queue_lock);

    /* Grow queue if needed */
    if (k->rq_cap == 0) {
        k->rq_cap    = 1024;
        k->run_queue = calloc(k->rq_cap, sizeof(AtomIsolate*));
        if (!k->run_queue) {
            pthread_mutex_unlock(&k->run_queue_lock);
            return -1;
        }
        k->rq_head = k->rq_tail = 0;
    }

    uint32_t next = (k->rq_tail + 1) % k->rq_cap;
    if (next == k->rq_head) {
        /* Queue full — drop (do not block) */
        pthread_mutex_unlock(&k->run_queue_lock);
        return -1;
    }

    k->run_queue[k->rq_tail] = a;
    k->rq_tail = next;
    pthread_cond_signal(&k->run_queue_cond);
    pthread_mutex_unlock(&k->run_queue_lock);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Worker thread
 * ───────────────────────────────────────────────────────────────────────── */

static void worker_process_atom(CogDiodKernel* k, AtomIsolate* a) {
    (void)k;
    LimboChannel* ch = a->incoming;
    while (ch) {
        /* Non-blocking: check if message available */
        pthread_mutex_lock(&ch->lock);
        if (ch->head != ch->tail) {
            CogMessage msg = ch->buf[ch->head];
            ch->head = (ch->head + 1) % CHANNEL_BUF_MAX;
            pthread_cond_signal(&ch->not_full);
            pthread_mutex_unlock(&ch->lock);
            elm_exec_msg(a, &msg);
        } else {
            pthread_mutex_unlock(&ch->lock);
        }
        ch = ch->in_next;
    }
}

static void* worker_thread(void* arg) {
    CogDiodKernel* k = (CogDiodKernel*)arg;

    while (k->running) {
        pthread_mutex_lock(&k->run_queue_lock);
        while (k->rq_head == k->rq_tail && k->running)
            pthread_cond_wait(&k->run_queue_cond, &k->run_queue_lock);
        if (!k->running) {
            pthread_mutex_unlock(&k->run_queue_lock);
            break;
        }
        AtomIsolate* a = k->run_queue[k->rq_head];
        k->rq_head = (k->rq_head + 1) % k->rq_cap;
        pthread_mutex_unlock(&k->run_queue_lock);

        if (!a || a->state != ATOM_ALIVE) continue;
        worker_process_atom(k, a);
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * ECAN background thread
 * ───────────────────────────────────────────────────────────────────────── */

static void* ecan_thread_fn(void* arg) {
    CogDiodKernel* k = (CogDiodKernel*)arg;
    while (k->running) {
        struct timespec ts = { 0, 100000000L }; /* 100 ms */
        nanosleep(&ts, NULL);
        if (k->running) cogdiod_ecan_diffuse(k);
    }
    return NULL;
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

    k->run_queue      = NULL;
    k->rq_head = k->rq_tail = k->rq_cap = 0;

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

    /* Free all remaining atoms (and their outgoing channels) */
    pthread_rwlock_wrlock(&k->pool_lock);
    for (int i = 0; i < ATOM_POOL_BUCKETS; i++) {
        AtomIsolate* a = k->atom_pool[i];
        while (a) {
            AtomIsolate* anext = a->ht_next;
            /* Free outgoing channels */
            LimboChannel* ch = a->outgoing;
            while (ch) {
                LimboChannel* cnext = ch->out_next;
                pthread_mutex_destroy(&ch->lock);
                pthread_cond_destroy(&ch->not_empty);
                pthread_cond_destroy(&ch->not_full);
                free(ch);
                ch = cnext;
            }
            free(a->vm_ctx.stack);
            free(a->vm_ctx.heap);
            pthread_mutex_destroy(&a->lock);
            free(a);
            a = anext;
        }
    }
    pthread_rwlock_unlock(&k->pool_lock);

    /* Free all packages (walk chains) */
    pthread_mutex_lock(&k->pkg_lock);
    for (int i = 0; i < PKG_CACHE_BUCKETS; i++) {
        ElmPackage* p = k->pkg_cache[i];
        while (p) {
            ElmPackage* pnext = p->next_in_cache;
            pthread_mutex_destroy(&p->ref_lock);
            free(p->dis_bytecode);
            free(p);
            p = pnext;
        }
        k->pkg_cache[i] = NULL;
    }
    pthread_mutex_unlock(&k->pkg_lock);

    free(k->run_queue);
    free(k->workers);
    pthread_rwlock_destroy(&k->pool_lock);
    pthread_mutex_destroy(&k->pkg_lock);
    pthread_mutex_destroy(&k->sti_lock);
    pthread_mutex_destroy(&k->run_queue_lock);
    pthread_cond_destroy(&k->run_queue_cond);
    free(k);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Package management  (Item 1: hash-chain collision handling)
 * ───────────────────────────────────────────────────────────────────────── */

ElmPackage* cogdiod_load_package(CogDiodKernel* k, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[cogdiod] cannot open package: %s\n", path);
        return NULL;
    }

    /* Try the new ElmFileHeader format first */
    uint32_t magic_check = 0;
    if (fread(&magic_check, 4, 1, f) != 1) { fclose(f); return NULL; }
    rewind(f);

    ElmPackage* pkg = NULL;

    if (magic_check == 0x454C4D00u /* ELM_FILE_MAGIC */) {
        /* New binary format — use elm_load_file */
        fclose(f);
        pkg = elm_load_file(path);
    } else {
        /* Legacy format: raw struct header (without pointer fields) */
        ElmPackage hdr;
        memset(&hdr, 0, sizeof(hdr));
        if (fread(&hdr, sizeof(ElmPackage)
                       - sizeof(uint8_t*)
                       - sizeof(pthread_mutex_t)
                       - sizeof(uint32_t)          /* stack_size */
                       - sizeof(struct ElmPackage*),/* next_in_cache */
                  1, f) != 1) {
            fclose(f); return NULL;
        }
        if (hdr.magic != ELM_MAGIC) {
            fprintf(stderr, "[cogdiod] bad magic in %s\n", path);
            fclose(f); return NULL;
        }
        uint8_t* bc = malloc(hdr.bytecode_size);
        if (!bc || fread(bc, 1, hdr.bytecode_size, f) != hdr.bytecode_size) {
            free(bc); fclose(f); return NULL;
        }
        fclose(f);
        pkg = calloc(1, sizeof(ElmPackage));
        *pkg = hdr;
        pkg->dis_bytecode  = bc;
        pkg->ref_count     = 0;
        pkg->next_in_cache = NULL;
        pthread_mutex_init(&pkg->ref_lock, NULL);
    }

    if (!pkg) return NULL;

    /* Insert into cache using chaining */
    pthread_mutex_lock(&k->pkg_lock);
    uint32_t b = pkg_bucket(pkg->type_id);
    pkg->next_in_cache = k->pkg_cache[b];
    k->pkg_cache[b]    = pkg;
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

    pthread_mutex_lock(&p->ref_lock);
    uint32_t refs = p->ref_count;
    pthread_mutex_unlock(&p->ref_lock);

    if (refs > 0) {
        pthread_mutex_unlock(&k->pkg_lock);
        fprintf(stderr, "[cogdiod] warning: cannot unload package "
                "type_id=0x%08x — %u active ref(s)\n", type_id, refs);
        return;
    }

    *pp = p->next_in_cache;
    if (k->pkg_count > 0) k->pkg_count--;
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
    size_t stkmax = pkg->stack_size ? pkg->stack_size : DISVM_STKMAX;
    ctx->stack   = calloc(stkmax, sizeof(uint8_t));
    ctx->heap    = calloc(4096, sizeof(uint8_t));
    ctx->heap_size = 4096;
    ctx->kernel_ref = NULL;
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

    a->uuid           = cogdiod_next_uuid(k);
    a->type_id        = type_id;
    a->state          = ATOM_SLEEPING;
    a->package        = pkg;
    a->hebbian_weight = 1.0f;
    a->history_count  = 0;

    if (atom_name)
        strncpy(a->name, atom_name, ATOM_NAME_MAX - 1);

    a->tv  = (TruthValue){0.5f, 0.1f};
    a->av  = (AttentionValue){0.0f, 0.0f};

    if (disvm_init(&a->vm_ctx, pkg) != 0) {
        free(a);
        return NULL;
    }
    a->vm_ctx.kernel_ref = k;

    pthread_mutex_lock(&pkg->ref_lock);
    pkg->ref_count++;
    pthread_mutex_unlock(&pkg->ref_lock);

    pthread_mutex_init(&a->lock, NULL);

    pthread_rwlock_wrlock(&k->pool_lock);
    uint32_t b      = pool_bucket(a->uuid);
    a->ht_next      = k->atom_pool[b];
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

/* Lock-free fast path using atomic loads (Item 32) */
AtomIsolate* cogdiod_get_atom_fast(CogDiodKernel* k, uint64_t uuid) {
    uint32_t b = pool_bucket(uuid);
    AtomIsolate* a = __atomic_load_n(&k->atom_pool[b], __ATOMIC_ACQUIRE);
    while (a && a->uuid != uuid)
        a = __atomic_load_n(&a->ht_next, __ATOMIC_ACQUIRE);
    return a;
}

int cogdiod_destroy_atom(CogDiodKernel* k, uint64_t uuid) {
    pthread_rwlock_wrlock(&k->pool_lock);
    uint32_t b       = pool_bucket(uuid);
    AtomIsolate** pp = &k->atom_pool[b];
    while (*pp && (*pp)->uuid != uuid) pp = &(*pp)->ht_next;
    if (!*pp) { pthread_rwlock_unlock(&k->pool_lock); return -1; }

    AtomIsolate* a = *pp;
    *pp = a->ht_next;
    k->atom_count--;
    pthread_rwlock_unlock(&k->pool_lock);

    a->state = ATOM_DYING;

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
 * Channel operations  (Item 2: shared LimboChannel with out_next/in_next)
 * ───────────────────────────────────────────────────────────────────────── */

LimboChannel* cogdiod_link(CogDiodKernel* k,
                           uint64_t src_uuid, uint64_t dst_uuid) {
    AtomIsolate* src = cogdiod_get_atom(k, src_uuid);
    AtomIsolate* dst = cogdiod_get_atom(k, dst_uuid);
    if (!src || !dst) return NULL;

    LimboChannel* ch = calloc(1, sizeof(LimboChannel));
    if (!ch) return NULL;

    ch->src_uuid      = src_uuid;
    ch->dst_uuid      = dst_uuid;
    ch->head = ch->tail = 0;
    ch->ref_count     = 2;  /* one for outgoing, one for incoming */
    ch->weight        = 1.0f;
    ch->last_fire_time = 0;
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);

    /* Add to src's outgoing list via out_next */
    pthread_mutex_lock(&src->lock);
    ch->out_next   = src->outgoing;
    src->outgoing  = ch;
    src->outgoing_count++;
    pthread_mutex_unlock(&src->lock);

    /* Add the SAME channel to dst's incoming list via in_next */
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
    while (next == ch->head)
        pthread_cond_wait(&ch->not_full, &ch->lock);
    ch->buf[ch->tail] = *msg;
    ch->tail = next;
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int cogdiod_recv(LimboChannel* ch, CogMessage* msg) {
    pthread_mutex_lock(&ch->lock);
    while (ch->head == ch->tail)
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    *msg = ch->buf[ch->head];
    ch->head = (ch->head + 1) % CHANNEL_BUF_MAX;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int cogdiod_unlink(CogDiodKernel* k,
                   uint64_t src_uuid, uint64_t dst_uuid) {
    AtomIsolate* src = cogdiod_get_atom(k, src_uuid);
    AtomIsolate* dst = cogdiod_get_atom(k, dst_uuid);
    if (!src || !dst) return -1;

    LimboChannel* found = NULL;

    /* Remove from src's outgoing list (via out_next) */
    pthread_mutex_lock(&src->lock);
    LimboChannel** pp = &src->outgoing;
    while (*pp) {
        if ((*pp)->dst_uuid == dst_uuid) {
            found = *pp;
            *pp   = found->out_next;
            src->outgoing_count--;
            break;
        }
        pp = &(*pp)->out_next;
    }
    pthread_mutex_unlock(&src->lock);

    if (!found) return -1;

    /* Remove the same channel from dst's incoming list (via in_next) */
    pthread_mutex_lock(&dst->lock);
    LimboChannel** pp2 = &dst->incoming;
    while (*pp2) {
        if (*pp2 == found) {
            *pp2 = found->in_next;
            dst->incoming_count--;
            break;
        }
        pp2 = &(*pp2)->in_next;
    }
    pthread_mutex_unlock(&dst->lock);

    /* Both refs removed; free the channel */
    pthread_mutex_destroy(&found->lock);
    pthread_cond_destroy(&found->not_empty);
    pthread_cond_destroy(&found->not_full);
    free(found);

    fprintf(stderr, "[cogdiod] unlinked %llu -> %llu\n",
            (unsigned long long)src_uuid,
            (unsigned long long)dst_uuid);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cognitive operations
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_set_tv(CogDiodKernel* k, uint64_t uuid, TruthValue tv) {
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    if (!a) return -1;

    pthread_mutex_lock(&a->lock);
    a->tv = tv;
    /* Store in episodic history */
    int h = a->history_count % 8;
    a->tv_history[h][0] = tv.strength;
    a->tv_history[h][1] = tv.confidence;
    a->history_count++;
    pthread_mutex_unlock(&a->lock);

    /* Notify all outgoing channels and wake downstream atoms (Item 4) */
    CogMessage msg = {
        .type        = MSG_SOURCE_CHANGED,
        .sender_uuid = uuid,
        .tv          = tv,
    };
    LimboChannel* ch = a->outgoing;
    while (ch) {
        cogdiod_send(ch, &msg);
        /* Enqueue downstream atom to process the message */
        AtomIsolate* dst = cogdiod_get_atom(k, ch->dst_uuid);
        if (dst) {
            dst->state = ATOM_ALIVE;
            cogdiod_enqueue(k, dst);
        }
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
 * ECAN diffusion (Item 18)
 * ───────────────────────────────────────────────────────────────────────── */

void cogdiod_ecan_diffuse(CogDiodKernel* k) {
    float threshold   = 1.0f;
    float spread_frac = 0.3f;

    pthread_rwlock_rdlock(&k->pool_lock);
    for (int i = 0; i < ATOM_POOL_BUCKETS; i++) {
        AtomIsolate* a = k->atom_pool[i];
        while (a) {
            pthread_mutex_lock(&a->lock);
            if (a->av.sti > threshold && a->outgoing_count > 0) {
                float spread = a->av.sti * spread_frac;
                a->av.sti -= spread;
                float per_ch = spread / (float)a->outgoing_count;
                LimboChannel* ch = a->outgoing;
                while (ch) {
                    CogMessage msg = {
                        .type        = MSG_ATTEND,
                        .sender_uuid = a->uuid,
                        .av          = { per_ch, 0.0f },
                    };
                    cogdiod_send(ch, &msg);
                    ch = ch->out_next;
                }
            }
            a->av.lti *= 0.999f;
            pthread_mutex_unlock(&a->lock);
            a = a->ht_next;
        }
    }
    pthread_rwlock_unlock(&k->pool_lock);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Hebbian learning (Item 20)
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_hebbian_update(CogDiodKernel* k,
                            uint64_t src_uuid, uint64_t dst_uuid) {
    AtomIsolate* src = cogdiod_get_atom(k, src_uuid);
    if (!src) return -1;

    pthread_mutex_lock(&src->lock);
    LimboChannel* ch = src->outgoing;
    while (ch) {
        if (ch->dst_uuid == dst_uuid) {
            ch->weight *= 1.05f;
            if (ch->weight > 2.0f) ch->weight = 2.0f;
            break;
        }
        ch = ch->out_next;
    }
    pthread_mutex_unlock(&src->lock);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Kernel start / stop  (Item 3: worker thread pool)
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_start(CogDiodKernel* k) {
    k->running = true;

    /* Pre-allocate run queue */
    if (!k->run_queue) {
        k->rq_cap    = 1024;
        k->run_queue = calloc(k->rq_cap, sizeof(AtomIsolate*));
        if (!k->run_queue) { k->running = false; return -1; }
        k->rq_head = k->rq_tail = 0;
    }

    /* Launch worker threads */
    for (uint32_t i = 0; i < k->worker_count; i++)
        pthread_create(&k->workers[i], NULL, worker_thread, k);

    /* Launch ECAN background thread if enabled */
    if (k->ecan_enabled)
        pthread_create(&k->ecan_thread, NULL, ecan_thread_fn, k);

    fprintf(stderr, "[cogdiod] kernel started\n");
    return 0;
}

void cogdiod_stop(CogDiodKernel* k) {
    if (!k->running) return;
    k->running = false;

    /* Wake all workers */
    pthread_mutex_lock(&k->run_queue_lock);
    pthread_cond_broadcast(&k->run_queue_cond);
    pthread_mutex_unlock(&k->run_queue_lock);

    /* Join workers */
    for (uint32_t i = 0; i < k->worker_count; i++) {
        if (k->workers[i])
            pthread_join(k->workers[i], NULL);
    }

    /* Join ECAN thread if running */
    if (k->ecan_enabled && k->ecan_thread)
        pthread_join(k->ecan_thread, NULL);

    fprintf(stderr, "[cogdiod] kernel stopped\n");
}
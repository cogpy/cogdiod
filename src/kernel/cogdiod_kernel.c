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

/* ─────────────────────────────────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * djb2 hash — used for both type_id computation and atom pool bucketing.
 */
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

    pthread_rwlock_init(&k->pool_lock, NULL);
    pthread_mutex_init(&k->pkg_lock, NULL);
    pthread_mutex_init(&k->sti_lock, NULL);

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

    /* Free all packages */
    pthread_mutex_lock(&k->pkg_lock);
    for (int i = 0; i < PKG_CACHE_BUCKETS; i++) {
        ElmPackage* p = k->pkg_cache[i];
        if (p) {
            free(p->dis_bytecode);
            free(p);
        }
    }
    pthread_mutex_unlock(&k->pkg_lock);

    free(k->workers);
    pthread_rwlock_destroy(&k->pool_lock);
    pthread_mutex_destroy(&k->pkg_lock);
    pthread_mutex_destroy(&k->sti_lock);
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
    pkg->dis_bytecode = bc;
    pkg->ref_count    = 0;
    pthread_mutex_init(&pkg->ref_lock, NULL);

    /* Insert into cache */
    pthread_mutex_lock(&k->pkg_lock);
    uint32_t b = pkg_bucket(pkg->type_id);
    k->pkg_cache[b] = pkg;   /* simple: one pkg per bucket for now */
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);

    fprintf(stderr, "[cogdiod] loaded package '%s' (type_id=0x%08x, %zu bytes)\n",
            pkg->name, pkg->type_id, pkg->bytecode_size);
    return pkg;
}

ElmPackage* cogdiod_get_package(CogDiodKernel* k, uint32_t type_id) {
    pthread_mutex_lock(&k->pkg_lock);
    ElmPackage* p = k->pkg_cache[pkg_bucket(type_id)];
    pthread_mutex_unlock(&k->pkg_lock);
    return (p && p->type_id == type_id) ? p : NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Dis VM isolate initialisation
 * ───────────────────────────────────────────────────────────────────────── */

static int disvm_init(DisVMContext* ctx, ElmPackage* pkg) {
    ctx->pc      = pkg->ep_init;
    ctx->sp      = 0;
    ctx->running = false;
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

    ch->src_uuid = src_uuid;
    ch->dst_uuid = dst_uuid;
    ch->head = ch->tail = 0;
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);

    /* Attach to both atoms */
    pthread_mutex_lock(&src->lock);
    ch->next = src->outgoing;
    src->outgoing = ch;
    src->outgoing_count++;
    pthread_mutex_unlock(&src->lock);

    pthread_mutex_lock(&dst->lock);
    /* dst keeps a pointer to the same channel object */
    LimboChannel* in_ch = calloc(1, sizeof(LimboChannel));
    *in_ch = *ch;
    in_ch->next = dst->incoming;
    dst->incoming = in_ch;
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

/* ─────────────────────────────────────────────────────────────────────────
 * Cognitive operations
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_set_tv(CogDiodKernel* k, uint64_t uuid, TruthValue tv) {
    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    if (!a) return -1;

    pthread_mutex_lock(&a->lock);
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
        ch = ch->next;
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
 * Kernel start / stop (stub — full 9P server in distyx.c)
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_start(CogDiodKernel* k) {
    k->running = true;
    fprintf(stderr, "[cogdiod] kernel started\n");
    return 0;
}

void cogdiod_stop(CogDiodKernel* k) {
    k->running = false;
    fprintf(stderr, "[cogdiod] kernel stopped\n");
}

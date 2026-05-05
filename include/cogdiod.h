/*
 * cogdiod.h — Core types for the CogDiod cognitive micro-kernel
 *
 * CogDiod is a self-contained cognitive daemon that manages a pool of
 * Dis VM isolates (Atoms), serves them over a 9P/DisTyx namespace, and
 * loads their behaviour from Elbo (.elm) packages.
 *
 * Architecture:
 *   CogDiodKernel
 *     ├── DisTyx9PServer      (9P/Styx protocol layer)
 *     ├── ElmPackageCache     (shared bytecode for each Atom type)
 *     ├── AtomIsolatePool     (active Dis VM execution contexts)
 *     └── CogScheduler        (STI-weighted thread pool)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────
 * 1.  PRIMITIVE COGNITIVE TYPES
 * ───────────────────────────────────────────────────────────────────────── */

/* SimpleTruthValue: strength ∈ [0,1], confidence ∈ [0,1] */
typedef struct {
    float strength;
    float confidence;
} TruthValue;

/* AttentionValue: STI (short-term) and LTI (long-term) importance */
typedef struct {
    float sti;   /* Short-Term Importance  — drives scheduling priority */
    float lti;   /* Long-Term Importance   — drives persistence */
} AttentionValue;

/* ─────────────────────────────────────────────────────────────────────────
 * 2.  DIS VM ISOLATE CONTEXT
 *     Minimal representation of one running Dis VM execution context.
 *     In a full implementation this wraps the actual DisVM registers,
 *     stack, and heap.  Here we provide the structural envelope.
 * ───────────────────────────────────────────────────────────────────────── */

#define DISVM_NREGS  16          /* Dis VM has 3 primary + general regs */
#define DISVM_STKMAX 4096        /* Max stack depth per isolate          */

typedef struct {
    uint64_t  pc;                /* Program counter (offset into bytecode) */
    uint64_t  regs[DISVM_NREGS]; /* General-purpose registers              */
    uint8_t*  stack;             /* Operand stack                          */
    size_t    sp;                /* Stack pointer                          */
    uint8_t*  heap;              /* Local heap                             */
    size_t    heap_size;
    bool      running;           /* Is this isolate currently scheduled?   */
    void*     kernel_ref;        /* Back-pointer to CogDiodKernel          */
} DisVMContext;

/* ─────────────────────────────────────────────────────────────────────────
 * 3.  ELM PACKAGE  (shared bytecode for one Atom type)
 * ───────────────────────────────────────────────────────────────────────── */

#define ELM_NAME_MAX  64
#define ELM_MAGIC     0x454C4D00  /* "ELM\0" */

typedef struct ElmPackage {
    uint32_t  magic;                  /* ELM_MAGIC                          */
    uint32_t  version;                /* Package format version             */
    uint32_t  type_id;                /* djb2 hash of the type name         */
    char      name[ELM_NAME_MAX];     /* Human-readable type name           */

    /* Compiled Dis bytecode (shared across all isolates of this type) */
    uint8_t*  dis_bytecode;
    size_t    bytecode_size;

    /* Handler entry-points (offsets into bytecode) */
    uint64_t  ep_init;                /* (init self)                        */
    uint64_t  ep_on_message;          /* (on-message self msg)              */
    uint64_t  ep_on_gc;               /* (on-gc self)  — before reclamation */

    /* Reference counting */
    uint32_t  ref_count;
    pthread_mutex_t ref_lock;

    /* Hash-table collision chaining */
    struct ElmPackage* next_in_cache;

    /* Nano-VM stack profile */
    uint32_t  stack_size;
} ElmPackage;

/* ─────────────────────────────────────────────────────────────────────────
 * 4.  LIMBO CHANNEL  (typed message channel between two isolates)
 * ───────────────────────────────────────────────────────────────────────── */

#define CHANNEL_BUF_MAX 64

typedef enum {
    MSG_UPDATE_TV     = 0x01,   /* TruthValue changed          */
    MSG_ATTEND        = 0x02,   /* STI transfer                */
    MSG_INFER         = 0x03,   /* Trigger inference cycle     */
    MSG_QUERY         = 0x04,   /* Pattern query               */
    MSG_SPAWN         = 0x05,   /* Request to spawn child atom */
    MSG_DESTROY       = 0x06,   /* Notify of impending GC      */
    MSG_SOURCE_CHANGED= 0x07,   /* Source atom state changed   */
    MSG_CUSTOM        = 0xFF,   /* Package-defined message     */
} MsgType;

typedef struct {
    MsgType    type;
    uint64_t   sender_uuid;
    union {
        TruthValue    tv;
        AttentionValue av;
        uint8_t        payload[56];
    };
} CogMessage;

typedef struct LimboChannel {
    uint64_t         src_uuid;
    uint64_t         dst_uuid;

    /* Ring buffer */
    CogMessage       buf[CHANNEL_BUF_MAX];
    uint32_t         head, tail;
    pthread_mutex_t  lock;
    pthread_cond_t   not_empty;
    pthread_cond_t   not_full;

    /* Shared-channel reference count (2 when live: one per endpoint list) */
    uint32_t         ref_count;

    /* Separate list pointers for src->outgoing and dst->incoming */
    struct LimboChannel* out_next;   /* Next in src atom's outgoing list */
    struct LimboChannel* in_next;    /* Next in dst atom's incoming list */

    /* Hebbian learning state */
    float            weight;
    uint64_t         last_fire_time;
} LimboChannel;

/* ─────────────────────────────────────────────────────────────────────────
 * 5.  ATOM ISOLATE  (one active Atom = one Dis VM context)
 * ───────────────────────────────────────────────────────────────────────── */

#define ATOM_NAME_MAX  128

typedef enum {
    ATOM_ALIVE    = 0,
    ATOM_SLEEPING = 1,   /* STI = 0, waiting for messages */
    ATOM_DYING    = 2,   /* Marked for GC                 */
} AtomState;

typedef struct AtomIsolate {
    /* Identity */
    uint64_t     uuid;                  /* Unique Qid for 9P namespace  */
    uint32_t     type_id;               /* Links to ElmPackage          */
    char         name[ATOM_NAME_MAX];   /* Optional symbolic name       */
    AtomState    state;

    /* Execution */
    DisVMContext vm_ctx;                /* The VM registers and stack   */
    ElmPackage*  package;              /* Shared bytecode (ref-counted) */

    /* Cognitive state */
    TruthValue   tv;
    AttentionValue av;

    /* Episodic TV history: [history_count][0]=strength [history_count][1]=conf */
    float        tv_history[8][2];
    int          history_count;

    /* Hebbian weight for this atom */
    float        hebbian_weight;

    /* Topology: intrusive linked lists of channels */
    LimboChannel* incoming;            /* Channels arriving at this atom */
    LimboChannel* outgoing;            /* Channels leaving this atom     */
    uint32_t      incoming_count;
    uint32_t      outgoing_count;

    /* Concurrency */
    pthread_mutex_t lock;
    pthread_t        thread;           /* OS thread (if scheduled)       */

    /* Hash-table linkage */
    struct AtomIsolate* ht_next;
} AtomIsolate;

/* ─────────────────────────────────────────────────────────────────────────
 * 6.  DISTYX 9P SERVER STATE
 * ───────────────────────────────────────────────────────────────────────── */

#define DISTYX_PORT_DEFAULT 9999
#define DISTYX_MSIZE        65536

typedef struct {
    int          listen_fd;
    uint16_t     port;
    uint32_t     msize;             /* Maximum 9P message size */
    pthread_t    accept_thread;
    bool         running;
} DisTyxServer;

/* ─────────────────────────────────────────────────────────────────────────
 * 7.  COGDIOD KERNEL  (the micro-kernel root)
 * ───────────────────────────────────────────────────────────────────────── */

#define ATOM_POOL_BUCKETS  4096
#define PKG_CACHE_BUCKETS  256

typedef struct {
    /* 9P/DisTyx server */
    DisTyxServer  server;

    /* Atom isolate pool: UUID -> AtomIsolate* (open-addressing hash table) */
    AtomIsolate*  atom_pool[ATOM_POOL_BUCKETS];
    uint64_t      atom_count;
    pthread_rwlock_t pool_lock;

    /* Package cache: type_id -> ElmPackage* (collision-chained) */
    ElmPackage*   pkg_cache[PKG_CACHE_BUCKETS];
    uint32_t      pkg_count;
    pthread_mutex_t pkg_lock;

    /* Worker thread pool */
    pthread_mutex_t  run_queue_lock;
    pthread_cond_t   run_queue_cond;
    AtomIsolate**    run_queue;
    uint32_t         rq_head, rq_tail, rq_cap;
    uint32_t         worker_count;
    pthread_t*       workers;

    /* ECAN background thread */
    pthread_t        ecan_thread;
    bool             ecan_enabled;

    /* Cognitive globals */
    float         total_sti;         /* Sum of all STI in the system */
    float         sti_funds;         /* Available STI to distribute  */
    pthread_mutex_t sti_lock;

    /* Lifecycle */
    bool          running;
    uint64_t      next_uuid;         /* Monotonic UUID counter       */
} CogDiodKernel;

/* ─────────────────────────────────────────────────────────────────────────
 * 8.  PUBLIC API
 * ───────────────────────────────────────────────────────────────────────── */

/* Kernel lifecycle */
CogDiodKernel* cogdiod_create(uint16_t port, uint32_t workers);
int            cogdiod_start(CogDiodKernel* k);
void           cogdiod_stop(CogDiodKernel* k);
void           cogdiod_destroy(CogDiodKernel* k);

/* Package management */
ElmPackage*    cogdiod_load_package(CogDiodKernel* k, const char* path);
ElmPackage*    cogdiod_get_package(CogDiodKernel* k, uint32_t type_id);
void           cogdiod_unload_package(CogDiodKernel* k, uint32_t type_id);

/* Atom lifecycle */
AtomIsolate*   cogdiod_spawn(CogDiodKernel* k, const char* type_name,
                             const char* atom_name);
AtomIsolate*   cogdiod_get_atom(CogDiodKernel* k, uint64_t uuid);
AtomIsolate*   cogdiod_get_atom_fast(CogDiodKernel* k, uint64_t uuid);
int            cogdiod_destroy_atom(CogDiodKernel* k, uint64_t uuid);

/* Channel operations */
LimboChannel*  cogdiod_link(CogDiodKernel* k,
                            uint64_t src_uuid, uint64_t dst_uuid);
int            cogdiod_unlink(CogDiodKernel* k,
                              uint64_t src_uuid, uint64_t dst_uuid);
int            cogdiod_send(LimboChannel* ch, const CogMessage* msg);
int            cogdiod_recv(LimboChannel* ch, CogMessage* msg);

/* Cognitive operations */
int            cogdiod_set_tv(CogDiodKernel* k, uint64_t uuid,
                              TruthValue tv);
TruthValue     cogdiod_get_tv(CogDiodKernel* k, uint64_t uuid);
int            cogdiod_attend(CogDiodKernel* k, uint64_t uuid, float sti);
void           cogdiod_enqueue(CogDiodKernel* k, AtomIsolate* a);
void           cogdiod_hebbian_update(CogDiodKernel* k,
                                      uint64_t src_uuid, uint64_t dst_uuid);

/* PLN reasoning rules */
TruthValue     pln_modus_ponens(TruthValue a, TruthValue ab);
TruthValue     pln_abduction(TruthValue a, TruthValue b, TruthValue ab);
TruthValue     pln_induction(TruthValue a, TruthValue b);
TruthValue     pln_temporal_deduce(TruthValue ab, TruthValue a,
                                   float steps, float decay);

/* Utilities */
uint32_t       cogdiod_hash_type(const char* name);
uint64_t       cogdiod_next_uuid(CogDiodKernel* k);

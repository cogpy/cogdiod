/*
 * elm_loader.c — Elbo (.elm) package builder and loader
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Defensive register bounds check
 * ───────────────────────────────────────────────────────────────────────── */

#define REG_BOUNDS_CHECK(idx) \
    do { \
        if ((size_t)(idx) >= DISVM_NREGS) { \
            fprintf(stderr, "[elm] reg OOB: %d\n", (int)(idx)); \
            return -1; \
        } \
    } while (0)

/* ─────────────────────────────────────────────────────────────────────────
 * PLN helpers (internal to VM; canonical implementations in pln.c)
 * ───────────────────────────────────────────────────────────────────────── */

static TruthValue pln_deduce(TruthValue ab, TruthValue a) {
    float s = ab.strength * a.strength;
    float c = ab.confidence * a.confidence * 0.9f;
    return (TruthValue){ s, c };
}

static TruthValue pln_revise(TruthValue tv1, TruthValue tv2) {
    float total_c = tv1.confidence + tv2.confidence;
    if (total_c < 1e-6f) return tv1;
    float s = (tv1.strength * tv1.confidence +
               tv2.strength * tv2.confidence) / total_c;
    float c = total_c / (total_c + 1.0f);
    return (TruthValue){ s, c };
}

/* ─────────────────────────────────────────────────────────────────────────
 * Minimal Dis VM interpreter
 * ───────────────────────────────────────────────────────────────────────── */

static int disvm_step(DisVMContext* ctx,
                      const uint8_t* bytecode, size_t bc_size,
                      AtomIsolate* self) {
    if (ctx->pc >= bc_size) return 1;

    DisOpcode op = (DisOpcode)bytecode[ctx->pc++];

    switch (op) {
    case OP_NOP:
        break;

    case OP_HALT:
        return 1;

    /* ── Arithmetic ─────────────────────────────────────────────────── */
    case OP_ADD:
        ctx->regs[0] = ctx->regs[1] + ctx->regs[2];
        break;

    case OP_SUB:
        ctx->regs[0] = ctx->regs[1] - ctx->regs[2];
        break;

    case OP_MUL:
        ctx->regs[0] = ctx->regs[1] * ctx->regs[2];
        break;

    case OP_DIV:
        if (ctx->regs[2] == 0) return -1;
        ctx->regs[0] = ctx->regs[1] / ctx->regs[2];
        break;

    /* ── Control flow ────────────────────────────────────────────────── */
    case OP_JMP:
        if (ctx->pc + 8 <= bc_size) {
            uint64_t target;
            memcpy(&target, bytecode + ctx->pc, 8);
            ctx->pc = target;
        }
        break;

    case OP_JEQ: {
        if (ctx->pc + 8 > bc_size) return -1;
        uint64_t target;
        memcpy(&target, bytecode + ctx->pc, 8);
        ctx->pc += 8;
        if (ctx->regs[0] == ctx->regs[1])
            ctx->pc = target;
        break;
    }

    case OP_JNE: {
        if (ctx->pc + 8 > bc_size) return -1;
        uint64_t target;
        memcpy(&target, bytecode + ctx->pc, 8);
        ctx->pc += 8;
        if (ctx->regs[0] != ctx->regs[1])
            ctx->pc = target;
        break;
    }

    case OP_CALL: {
        if (ctx->pc + 8 > bc_size) return -1;
        uint64_t target;
        memcpy(&target, bytecode + ctx->pc, 8);
        uint64_t ret_addr = ctx->pc + 8;
        /* Push return address onto stack (8 bytes per frame slot) */
        size_t sp_bytes = ctx->sp * 8;
        if (sp_bytes + 8 <= DISVM_STKMAX) {
            memcpy(ctx->stack + sp_bytes, &ret_addr, 8);
            ctx->sp++;
        }
        ctx->pc = target;
        break;
    }

    case OP_RET: {
        if (ctx->sp == 0) return 1;
        ctx->sp--;
        uint64_t ret_addr;
        memcpy(&ret_addr, ctx->stack + ctx->sp * 8, 8);
        ctx->pc = ret_addr;
        break;
    }

    /* ── Memory ──────────────────────────────────────────────────────── */
    case OP_LOAD: {
        uint64_t addr = ctx->regs[1];
        if (addr < ctx->heap_size)
            ctx->regs[0] = ctx->heap[addr];
        break;
    }

    case OP_STORE: {
        uint64_t addr = ctx->regs[1];
        if (addr < ctx->heap_size)
            ctx->heap[addr] = (uint8_t)(ctx->regs[0] & 0xFF);
        break;
    }

    case OP_PUSH: {
        size_t sp_bytes = ctx->sp * 8;
        if (sp_bytes + 8 <= DISVM_STKMAX) {
            memcpy(ctx->stack + sp_bytes, &ctx->regs[0], 8);
            ctx->sp++;
        }
        break;
    }

    case OP_POP: {
        if (ctx->sp == 0) break;
        ctx->sp--;
        memcpy(&ctx->regs[0], ctx->stack + ctx->sp * 8, 8);
        break;
    }

    /* ── Channel operations ──────────────────────────────────────────── */
    case OP_SEND: {
        uint64_t ch_idx = ctx->regs[0];
        LimboChannel* ch = self->outgoing;
        uint64_t i = 0;
        while (ch && i < ch_idx) { ch = ch->out_next; i++; }
        if (ch) {
            CogMessage msg = {
                .type        = MSG_CUSTOM,
                .sender_uuid = self->uuid,
            };
            memcpy(&msg.payload, &ctx->regs[1],
                   sizeof(msg.payload) < 8 ? sizeof(msg.payload) : 8);
            cogdiod_send(ch, &msg);
        }
        break;
    }

    case OP_RECV: {
        LimboChannel* ch = self->incoming;
        if (ch) {
            pthread_mutex_lock(&ch->lock);
            if (ch->head != ch->tail) {
                CogMessage msg = ch->buf[ch->head];
                ch->head = (ch->head + 1) % CHANNEL_BUF_MAX;
                pthread_cond_signal(&ch->not_full);
                pthread_mutex_unlock(&ch->lock);
                ctx->regs[0] = msg.type;
                ctx->regs[1] = msg.sender_uuid;
            } else {
                pthread_mutex_unlock(&ch->lock);
                ctx->regs[0] = 0;
            }
        }
        break;
    }

    case OP_SPAWN: {
        if (self->vm_ctx.kernel_ref) {
            CogDiodKernel* k = (CogDiodKernel*)self->vm_ctx.kernel_ref;
            char type_buf[ELM_NAME_MAX];
            snprintf(type_buf, sizeof(type_buf), "type_%llu",
                     (unsigned long long)ctx->regs[0]);
            cogdiod_spawn(k, type_buf, NULL);
        }
        break;
    }

    /* ── Cognitive operations ────────────────────────────────────────── */
    case OP_GET_TV:
        memcpy(&ctx->regs[0], &self->tv.strength,   4);
        memcpy(&ctx->regs[1], &self->tv.confidence, 4);
        break;

    case OP_SET_TV: {
        TruthValue tv;
        memcpy(&tv.strength,   &ctx->regs[0], 4);
        memcpy(&tv.confidence, &ctx->regs[1], 4);
        self->tv = tv;
        break;
    }

    case OP_GET_STI:
        memcpy(&ctx->regs[0], &self->av.sti, 4);
        break;

    case OP_SET_STI:
        memcpy(&self->av.sti, &ctx->regs[0], 4);
        break;

    case OP_PLN_DED: {
        TruthValue a, ab;
        memcpy(&a.strength,    &ctx->regs[0], 4);
        memcpy(&a.confidence,  &ctx->regs[1], 4);
        memcpy(&ab.strength,   &ctx->regs[2], 4);
        memcpy(&ab.confidence, &ctx->regs[3], 4);
        TruthValue result = pln_deduce(ab, a);
        memcpy(&ctx->regs[4], &result.strength,   4);
        memcpy(&ctx->regs[5], &result.confidence, 4);
        break;
    }

    case OP_PLN_REV: {
        TruthValue tv1, tv2;
        memcpy(&tv1.strength,   &ctx->regs[0], 4);
        memcpy(&tv1.confidence, &ctx->regs[1], 4);
        memcpy(&tv2.strength,   &ctx->regs[2], 4);
        memcpy(&tv2.confidence, &ctx->regs[3], 4);
        TruthValue result = pln_revise(tv1, tv2);
        memcpy(&ctx->regs[4], &result.strength,   4);
        memcpy(&ctx->regs[5], &result.confidence, 4);
        break;
    }

    case OP_PLN_ABD: {
        /* regs[0..1]=a, regs[2..3]=b, regs[4..5]=ab → regs[6..7] */
        TruthValue a, b, ab;
        memcpy(&a.strength,    &ctx->regs[0], 4);
        memcpy(&a.confidence,  &ctx->regs[1], 4);
        memcpy(&b.strength,    &ctx->regs[2], 4);
        memcpy(&b.confidence,  &ctx->regs[3], 4);
        memcpy(&ab.strength,   &ctx->regs[4], 4);
        memcpy(&ab.confidence, &ctx->regs[5], 4);
        TruthValue result = pln_abduction(a, b, ab);
        REG_BOUNDS_CHECK(7);
        memcpy(&ctx->regs[6], &result.strength,   4);
        memcpy(&ctx->regs[7], &result.confidence, 4);
        break;
    }

    case OP_PLN_IND: {
        /* regs[0..1]=a, regs[2..3]=b → regs[4..5] */
        TruthValue a, b;
        memcpy(&a.strength,   &ctx->regs[0], 4);
        memcpy(&a.confidence, &ctx->regs[1], 4);
        memcpy(&b.strength,   &ctx->regs[2], 4);
        memcpy(&b.confidence, &ctx->regs[3], 4);
        TruthValue result = pln_induction(a, b);
        memcpy(&ctx->regs[4], &result.strength,   4);
        memcpy(&ctx->regs[5], &result.confidence, 4);
        break;
    }

    case OP_PLN_TMP: {
        /* regs[0..1]=ab, regs[2..3]=a, regs[4]=steps, regs[5]=decay */
        TruthValue ab, a;
        float steps, decay;
        memcpy(&ab.strength,   &ctx->regs[0], 4);
        memcpy(&ab.confidence, &ctx->regs[1], 4);
        memcpy(&a.strength,    &ctx->regs[2], 4);
        memcpy(&a.confidence,  &ctx->regs[3], 4);
        memcpy(&steps,         &ctx->regs[4], 4);
        memcpy(&decay,         &ctx->regs[5], 4);
        TruthValue result = pln_temporal_deduce(ab, a, steps, decay);
        REG_BOUNDS_CHECK(7);
        memcpy(&ctx->regs[6], &result.strength,   4);
        memcpy(&ctx->regs[7], &result.confidence, 4);
        break;
    }

    case OP_ECAN_SP: {
        float spread = self->av.sti * 0.5f;
        self->av.sti -= spread;
        LimboChannel* ch = self->outgoing;
        uint32_t n = self->outgoing_count;
        if (n > 0) {
            float per_ch = spread / (float)n;
            while (ch) {
                CogMessage msg = {
                    .type        = MSG_ATTEND,
                    .sender_uuid = self->uuid,
                    .av          = { per_ch, 0.0f },
                };
                cogdiod_send(ch, &msg);
                ch = ch->out_next;
            }
        }
        break;
    }

    default:
        break;
    }
    return 0;
}

static void disvm_run(DisVMContext* ctx,
                      const uint8_t* bytecode, size_t bc_size,
                      AtomIsolate* self) {
    ctx->running = true;
    int r = 0;
    while (r == 0)
        r = disvm_step(ctx, bytecode, bc_size, self);
    ctx->running = false;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Elm package execution entry points
 * ───────────────────────────────────────────────────────────────────────── */

int elm_exec_init(AtomIsolate* a) {
    if (!a || !a->package) return -1;
    a->vm_ctx.pc = a->package->ep_init;
    disvm_run(&a->vm_ctx,
              a->package->dis_bytecode,
              a->package->bytecode_size,
              a);
    return 0;
}

int elm_exec_msg(AtomIsolate* a, const CogMessage* msg) {
    if (!a || !a->package) return -1;

    a->vm_ctx.regs[8]  = msg->type;
    a->vm_ctx.regs[9]  = msg->sender_uuid;
    memcpy(&a->vm_ctx.regs[10], &msg->tv.strength,   4);
    memcpy(&a->vm_ctx.regs[11], &msg->tv.confidence, 4);

    a->vm_ctx.pc = a->package->ep_on_message;
    disvm_run(&a->vm_ctx,
              a->package->dis_bytecode,
              a->package->bytecode_size,
              a);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Stub package builder
 * ───────────────────────────────────────────────────────────────────────── */

ElmPackage* elm_build_stub(const ElmStubDef* def) {
    ElmPackage* pkg = calloc(1, sizeof(ElmPackage));
    if (!pkg) return NULL;

    pkg->magic   = ELM_MAGIC;
    pkg->version = 1;
    pkg->type_id = cogdiod_hash_type(def->type_name);
    strncpy(pkg->name, def->type_name, ELM_NAME_MAX - 1);

    size_t total = def->init_bc_len + def->msg_bc_len + def->gc_bc_len;
    pkg->dis_bytecode  = malloc(total);
    pkg->bytecode_size = total;

    size_t off = 0;
    pkg->ep_init = off;
    memcpy(pkg->dis_bytecode + off, def->init_bc, def->init_bc_len);
    off += def->init_bc_len;

    pkg->ep_on_message = off;
    memcpy(pkg->dis_bytecode + off, def->msg_bc, def->msg_bc_len);
    off += def->msg_bc_len;

    pkg->ep_on_gc = off;
    memcpy(pkg->dis_bytecode + off, def->gc_bc, def->gc_bc_len);

    pkg->ref_count     = 0;
    pkg->next_in_cache = NULL;
    pthread_mutex_init(&pkg->ref_lock, NULL);

    fprintf(stderr, "[elm] built stub package '%s' (type_id=0x%08x, %zu bytes)\n",
            pkg->name, pkg->type_id, pkg->bytecode_size);
    return pkg;
}

int elm_save(const ElmPackage* pkg, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(pkg, sizeof(ElmPackage) - sizeof(uint8_t*) - sizeof(pthread_mutex_t)
               - sizeof(struct ElmPackage*) - sizeof(uint32_t), 1, f);
    fwrite(pkg->dis_bytecode, 1, pkg->bytecode_size, f);
    fclose(f);
    fprintf(stderr, "[elm] saved '%s' to %s\n", pkg->name, path);
    return 0;
}

/*
 * elm_loader.c — Elbo (.elm) package builder, loader, and Dis VM executor
 *
 * Includes:
 *   - REG_BOUNDS_CHECK defensive assertions (Item 5)
 *   - Full opcode set: ADD/SUB/MUL/DIV/JEQ/JNE/CALL/RET/LOAD/STORE/PUSH/POP (Item 6)
 *   - OP_SEND / OP_RECV channel opcodes (Item 8)
 *   - OP_SPAWN with kernel back-pointer (Item 9)
 *   - PLN abduction/induction/temporal opcodes (Item 17)
 *   - elm_load_file using ElmFileHeader format (Item 10)
 *   - elm_exec_msg with atom lock for thread safety
 */

#include "cogdiod.h"
#include "elm_types.h"
#include "elm_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Bounds-check macro for register accesses (Item 5)
 * ───────────────────────────────────────────────────────────────────────── */

#define REG_BOUNDS_CHECK(idx) \
    do { if ((uint64_t)(idx) >= DISVM_NREGS) { \
        fprintf(stderr, "[elm] register index %u out of bounds\n", (unsigned)(idx)); \
        return -1; \
    } } while(0)

/* ─────────────────────────────────────────────────────────────────────────
 * PLN inference rules (also exposed via cogdiod.h declarations)
 * ───────────────────────────────────────────────────────────────────────── */

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

/* ─────────────────────────────────────────────────────────────────────────
 * Minimal Dis VM interpreter
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * Execute one step of the Dis VM.
 * Returns 0 to continue, 1 to halt, -1 on error.
 */
static int disvm_step(DisVMContext* ctx,
                      const uint8_t* bytecode, size_t bc_size,
                      AtomIsolate* self) {
    if (ctx->pc >= bc_size) return 1;

    DisOpcode op = (DisOpcode)bytecode[ctx->pc++];
    size_t stkmax = self->package->stack_size
                    ? self->package->stack_size : DISVM_STKMAX;

    switch (op) {
    case OP_NOP:
        break;

    case OP_HALT:
        return 1;

    /* ── Arithmetic (Item 6) ─────────────────────────────────────────── */
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

    /* ── Control flow (Item 6) ──────────────────────────────────────── */
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
        if (ctx->regs[0] == ctx->regs[1]) ctx->pc = target;
        break;
    }

    case OP_JNE: {
        if (ctx->pc + 8 > bc_size) return -1;
        uint64_t target;
        memcpy(&target, bytecode + ctx->pc, 8);
        ctx->pc += 8;
        if (ctx->regs[0] != ctx->regs[1]) ctx->pc = target;
        break;
    }

    case OP_CALL: {
        if (ctx->pc + 8 > bc_size) return -1;
        uint64_t target;
        memcpy(&target, bytecode + ctx->pc, 8);
        ctx->pc += 8;
        if (ctx->sp + 8 <= stkmax) {
            memcpy(ctx->stack + ctx->sp, &ctx->pc, 8);
            ctx->sp += 8;
        }
        ctx->pc = target;
        break;
    }

    case OP_RET:
        if (ctx->sp >= 8) {
            ctx->sp -= 8;
            memcpy(&ctx->pc, ctx->stack + ctx->sp, 8);
        } else {
            return 1; /* stack underflow = halt */
        }
        break;

    /* ── Memory (Item 6) ─────────────────────────────────────────────── */
    case OP_LOAD: {
        uint64_t addr = ctx->regs[1];
        if (addr + 8 <= ctx->heap_size)
            memcpy(&ctx->regs[0], ctx->heap + addr, 8);
        break;
    }

    case OP_STORE: {
        uint64_t addr = ctx->regs[1];
        if (addr + 8 <= ctx->heap_size)
            memcpy(ctx->heap + addr, &ctx->regs[0], 8);
        break;
    }

    case OP_PUSH:
        if (ctx->sp + 8 <= stkmax) {
            memcpy(ctx->stack + ctx->sp, &ctx->regs[0], 8);
            ctx->sp += 8;
        }
        break;

    case OP_POP:
        if (ctx->sp >= 8) {
            ctx->sp -= 8;
            memcpy(&ctx->regs[0], ctx->stack + ctx->sp, 8);
        }
        break;

    /* ── Channel operations (Items 8, 9) ────────────────────────────── */
    case OP_SEND: {
        /* regs[0] = channel index (into self->outgoing, 0-based) */
        uint32_t idx = (uint32_t)ctx->regs[0];
        LimboChannel* ch = self->outgoing;
        for (uint32_t i = 0; i < idx && ch; i++) ch = ch->out_next;
        if (ch) {
            CogMessage msg = {
                .type        = MSG_UPDATE_TV,
                .sender_uuid = self->uuid,
            };
            memcpy(&msg.tv.strength,   &ctx->regs[1], 4);
            memcpy(&msg.tv.confidence, &ctx->regs[2], 4);
            cogdiod_send(ch, &msg);
        }
        break;
    }

    case OP_RECV: {
        /* Non-blocking recv from first incoming channel with a message */
        LimboChannel* ch = self->incoming;
        if (ch) {
            pthread_mutex_lock(&ch->lock);
            if (ch->head != ch->tail) {
                CogMessage msg = ch->buf[ch->head];
                ch->head = (ch->head + 1) % CHANNEL_BUF_MAX;
                pthread_cond_signal(&ch->not_full);
                pthread_mutex_unlock(&ch->lock);
                REG_BOUNDS_CHECK(8);
                REG_BOUNDS_CHECK(9);
                REG_BOUNDS_CHECK(10);
                REG_BOUNDS_CHECK(11);
                ctx->regs[8] = msg.type;
                ctx->regs[9] = msg.sender_uuid;
                memcpy(&ctx->regs[10], &msg.tv.strength,   4);
                memcpy(&ctx->regs[11], &msg.tv.confidence, 4);
            } else {
                pthread_mutex_unlock(&ch->lock);
            }
        }
        break;
    }

    case OP_SPAWN: {
        /* Spawn a ConceptNode via kernel back-pointer */
        if (!self->vm_ctx.kernel_ref) break;
        CogDiodKernel* k = (CogDiodKernel*)self->vm_ctx.kernel_ref;
        AtomIsolate* child = cogdiod_spawn(k, "ConceptNode", NULL);
        if (child) ctx->regs[0] = child->uuid;
        else       ctx->regs[0] = 0;
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

    case OP_ECAN_SP: {
        /* Spread half of STI to outgoing channels */
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

    /* ── PLN extended rules (Item 17) ───────────────────────────────── */
    case OP_PLN_ABD: {
        /* Abduction: regs[0..1]=a, regs[2..3]=b, regs[4..5]=ab → regs[6..7] */
        TruthValue a, b, ab;
        memcpy(&a.strength,    &ctx->regs[0], 4);
        memcpy(&a.confidence,  &ctx->regs[1], 4);
        memcpy(&b.strength,    &ctx->regs[2], 4);
        memcpy(&b.confidence,  &ctx->regs[3], 4);
        memcpy(&ab.strength,   &ctx->regs[4], 4);
        memcpy(&ab.confidence, &ctx->regs[5], 4);
        TruthValue result = pln_abduction(a, b, ab);
        memcpy(&ctx->regs[6], &result.strength,   4);
        memcpy(&ctx->regs[7], &result.confidence, 4);
        break;
    }

    case OP_PLN_IND: {
        /* Induction: regs[0..1]=a, regs[2..3]=b → regs[4..5] */
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
        /* Temporal deduction: regs[0..1]=ab, regs[2..3]=a,
         * regs[4]=time_steps, regs[5]=decay → regs[6..7] */
        TruthValue ab, a;
        memcpy(&ab.strength,   &ctx->regs[0], 4);
        memcpy(&ab.confidence, &ctx->regs[1], 4);
        memcpy(&a.strength,    &ctx->regs[2], 4);
        memcpy(&a.confidence,  &ctx->regs[3], 4);
        float time_steps, decay;
        memcpy(&time_steps, &ctx->regs[4], 4);
        memcpy(&decay,      &ctx->regs[5], 4);
        TruthValue result = pln_temporal_deduce(ab, a, time_steps, decay);
        memcpy(&ctx->regs[6], &result.strength,   4);
        memcpy(&ctx->regs[7], &result.confidence, 4);
        break;
    }

    default:
        /* Unknown opcode — skip */
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
    pthread_mutex_lock(&a->lock);
    a->vm_ctx.pc = a->package->ep_init;
    disvm_run(&a->vm_ctx,
              a->package->dis_bytecode,
              a->package->bytecode_size,
              a);
    pthread_mutex_unlock(&a->lock);
    return 0;
}

int elm_exec_msg(AtomIsolate* a, const CogMessage* msg) {
    if (!a || !a->package) return -1;

    /* Serialize VM execution per atom */
    pthread_mutex_lock(&a->lock);

    REG_BOUNDS_CHECK(8);
    REG_BOUNDS_CHECK(9);
    REG_BOUNDS_CHECK(10);
    REG_BOUNDS_CHECK(11);

    a->vm_ctx.regs[8]  = msg->type;
    a->vm_ctx.regs[9]  = msg->sender_uuid;
    memcpy(&a->vm_ctx.regs[10], &msg->tv.strength,   4);
    memcpy(&a->vm_ctx.regs[11], &msg->tv.confidence, 4);

    a->vm_ctx.pc = a->package->ep_on_message;
    disvm_run(&a->vm_ctx,
              a->package->dis_bytecode,
              a->package->bytecode_size,
              a);

    pthread_mutex_unlock(&a->lock);
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

/* ─────────────────────────────────────────────────────────────────────────
 * Save / load .elm files  (Item 10: ElmFileHeader format)
 * ───────────────────────────────────────────────────────────────────────── */

int elm_save(const ElmPackage* pkg, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;

    ElmFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = ELM_FILE_MAGIC;
    hdr.version       = ELM_FILE_VERSION;
    hdr.type_id       = pkg->type_id;
    hdr.flags         = 0;
    hdr.ep_init       = pkg->ep_init;
    hdr.ep_on_message = pkg->ep_on_message;
    hdr.ep_on_gc      = pkg->ep_on_gc;
    hdr.bytecode_size = (uint32_t)pkg->bytecode_size;
    hdr.symtab_count  = 0;
    strncpy(hdr.type_name, pkg->name, 63);

    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(pkg->dis_bytecode, 1, pkg->bytecode_size, f);
    fclose(f);
    fprintf(stderr, "[elm] saved '%s' to %s\n", pkg->name, path);
    return 0;
}

ElmPackage* elm_load_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    ElmFileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 ||
        hdr.magic != ELM_FILE_MAGIC) {
        fclose(f);
        return NULL;
    }

    uint8_t* bc = malloc(hdr.bytecode_size);
    if (!bc || fread(bc, 1, hdr.bytecode_size, f) != hdr.bytecode_size) {
        free(bc);
        fclose(f);
        return NULL;
    }
    fclose(f);

    ElmPackage* pkg = calloc(1, sizeof(ElmPackage));
    if (!pkg) { free(bc); return NULL; }

    pkg->magic         = ELM_MAGIC;
    pkg->version       = hdr.version;
    pkg->type_id       = hdr.type_id;
    pkg->ep_init       = hdr.ep_init;
    pkg->ep_on_message = hdr.ep_on_message;
    pkg->ep_on_gc      = hdr.ep_on_gc;
    pkg->dis_bytecode  = bc;
    pkg->bytecode_size = hdr.bytecode_size;
    pkg->ref_count     = 0;
    pkg->next_in_cache = NULL;
    strncpy(pkg->name, hdr.type_name, ELM_NAME_MAX - 1);
    pthread_mutex_init(&pkg->ref_lock, NULL);

    fprintf(stderr, "[elm] loaded '%s' from %s (%u bytes)\n",
            pkg->name, path, hdr.bytecode_size);
    return pkg;
}
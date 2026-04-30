/*
 * elm_loader.c — Elbo (.elm) package builder and loader
 *
 * An .elm file is a binary package with the following layout:
 *
 *   [ElmHeader]          — fixed-size header (see elm_format.h)
 *   [Dis bytecode]       — compiled Limbo instructions
 *   [Symbol table]       — optional: name → bytecode offset map
 *
 * This file provides:
 *   elm_build_stub()  — builds a stub .elm from a C behaviour table
 *   elm_load_file()   — loads an .elm file from disk
 *   elm_exec_init()   — runs the (init) entry point in an isolate
 *   elm_exec_msg()    — dispatches a CogMessage to an isolate
 *
 * The "Dis VM" in this implementation is a minimal interpreter that
 * understands a small set of cognitive opcodes.  A full implementation
 * would embed the real Dis VM from the Inferno OS source tree.
 */

#include "cogdiod.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ─────────────────────────────────────────────────────────────────────────
 * Minimal Dis VM interpreter
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * PLN deduction: given P(A->B) and P(A), compute P(B).
 * Uses the simple independence assumption: s_B = s_AB * s_A
 */
static TruthValue pln_deduce(TruthValue ab, TruthValue a) {
    float s = ab.strength * a.strength;
    float c = ab.confidence * a.confidence * 0.9f;  /* confidence decay */
    return (TruthValue){ s, c };
}

/*
 * PLN revision: merge two independent observations of the same truth.
 * Uses weighted average by confidence.
 */
static TruthValue pln_revise(TruthValue tv1, TruthValue tv2) {
    float total_c = tv1.confidence + tv2.confidence;
    if (total_c < 1e-6f) return tv1;
    float s = (tv1.strength * tv1.confidence +
               tv2.strength * tv2.confidence) / total_c;
    float c = total_c / (total_c + 1.0f);   /* count-based update */
    return (TruthValue){ s, c };
}

/*
 * Execute one step of the Dis VM.
 * Returns 0 to continue, 1 to halt, -1 on error.
 */
static int disvm_step(DisVMContext* ctx,
                      const uint8_t* bytecode, size_t bc_size,
                      AtomIsolate* self) {
    if (ctx->pc >= bc_size) return 1;  /* halt at end */

    DisOpcode op = (DisOpcode)bytecode[ctx->pc++];

    switch (op) {
    case OP_NOP:
        break;

    case OP_HALT:
        return 1;

    case OP_GET_TV:
        /* Load TruthValue into regs[0] (strength) and regs[1] (confidence) */
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
        /* regs[0..1] = TV of antecedent, regs[2..3] = TV of implication */
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
            float per_ch = spread / n;
            while (ch) {
                CogMessage msg = {
                    .type        = MSG_ATTEND,
                    .sender_uuid = self->uuid,
                    .av          = { per_ch, 0.0f },
                };
                cogdiod_send(ch, &msg);
                ch = ch->next;
            }
        }
        break;
    }

    case OP_JMP:
        if (ctx->pc + 8 <= bc_size) {
            uint64_t target;
            memcpy(&target, bytecode + ctx->pc, 8);
            ctx->pc = target;
        }
        break;

    default:
        /* Unknown opcode — skip */
        break;
    }
    return 0;
}

/*
 * Run the VM until HALT or end of bytecode.
 */
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

    /* Load message into VM registers before calling on-message */
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
 *
 * Builds a minimal .elm package from a C behaviour description.
 * Used to bootstrap the archetypal packages before a real Elbo
 * compiler is available.
 * ───────────────────────────────────────────────────────────────────────── */


ElmPackage* elm_build_stub(const ElmStubDef* def) {
    ElmPackage* pkg = calloc(1, sizeof(ElmPackage));
    if (!pkg) return NULL;

    pkg->magic   = ELM_MAGIC;
    pkg->version = 1;
    pkg->type_id = cogdiod_hash_type(def->type_name);
    strncpy(pkg->name, def->type_name, ELM_NAME_MAX - 1);

    /* Layout bytecode: [init][msg][gc] */
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

    pkg->ref_count = 0;
    pthread_mutex_init(&pkg->ref_lock, NULL);

    fprintf(stderr, "[elm] built stub package '%s' (type_id=0x%08x, %zu bytes)\n",
            pkg->name, pkg->type_id, pkg->bytecode_size);
    return pkg;
}

/*
 * Save an ElmPackage to disk as a .elm file.
 */
int elm_save(const ElmPackage* pkg, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;

    /* Write header (excluding the pointer fields) */
    fwrite(pkg, sizeof(ElmPackage) - sizeof(uint8_t*) - sizeof(pthread_mutex_t), 1, f);
    fwrite(pkg->dis_bytecode, 1, pkg->bytecode_size, f);
    fclose(f);
    fprintf(stderr, "[elm] saved '%s' to %s\n", pkg->name, path);
    return 0;
}

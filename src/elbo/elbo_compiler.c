/*
 * elbo_compiler.c — Elbo S-expression → Dis bytecode compiler (Item 11)
 *
 * Grammar (subset):
 *   expr  ::= atom | '(' head expr* ')'
 *   head  ::= 'concept' | 'implication' | 'evaluation'
 *           | 'pln-ded' | 'pln-rev' | 'pln-abd' | 'pln-ind' | 'pln-tmp'
 *           | 'set-tv' | 'get-tv' | 'set-sti' | 'get-sti'
 *           | 'send' | 'recv' | 'spawn' | 'halt'
 */

#include "elbo_compiler.h"
#include "elm_types.h"
#include "cogdiod.h"
#include "elm_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Bytecode emitter
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t* buf;
    size_t   size;
    size_t   cap;
} ByteBuffer;

static int bb_ensure(ByteBuffer* b, size_t need) {
    if (b->size + need <= b->cap) return 0;
    size_t newcap = b->cap ? b->cap * 2 : 64;
    while (newcap < b->size + need) newcap *= 2;
    uint8_t* p = realloc(b->buf, newcap);
    if (!p) return -1;
    b->buf = p; b->cap = newcap;
    return 0;
}

static int bb_emit(ByteBuffer* b, uint8_t byte) {
    if (bb_ensure(b, 1) != 0) return -1;
    b->buf[b->size++] = byte;
    return 0;
}

static int bb_emit_u64(ByteBuffer* b, uint64_t v) {
    if (bb_ensure(b, 8) != 0) return -1;
    for (int i = 0; i < 8; i++)
        b->buf[b->size++] = (v >> (i * 8)) & 0xFF;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Tokeniser
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char* src;
    size_t      pos;
    size_t      len;
} Lexer;

static void lex_skip(Lexer* l) {
    while (l->pos < l->len && isspace((unsigned char)l->src[l->pos]))
        l->pos++;
    /* skip line comments */
    while (l->pos < l->len && l->src[l->pos] == ';') {
        while (l->pos < l->len && l->src[l->pos] != '\n') l->pos++;
        while (l->pos < l->len && isspace((unsigned char)l->src[l->pos]))
            l->pos++;
    }
}

/* Read until whitespace or paren; writes into out (max out_max-1 chars) */
static int lex_atom(Lexer* l, char* out, size_t out_max) {
    lex_skip(l);
    if (l->pos >= l->len) return 0;
    size_t n = 0;
    while (l->pos < l->len && n < out_max - 1) {
        char c = l->src[l->pos];
        if (isspace((unsigned char)c) || c == '(' || c == ')') break;
        out[n++] = c;
        l->pos++;
    }
    out[n] = '\0';
    return (int)n;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Parser / code-generator
 * ───────────────────────────────────────────────────────────────────────── */

static int compile_expr(Lexer* l, ByteBuffer* b);

static int compile_list(Lexer* l, ByteBuffer* b) {
    /* Already consumed '(' */
    char head[64];
    if (lex_atom(l, head, sizeof(head)) <= 0) return -1;

    if (strcmp(head, "halt") == 0) {
        bb_emit(b, OP_HALT);
    } else if (strcmp(head, "nop") == 0) {
        bb_emit(b, OP_NOP);
    } else if (strcmp(head, "get-tv") == 0) {
        bb_emit(b, OP_GET_TV);
    } else if (strcmp(head, "set-tv") == 0) {
        compile_expr(l, b);   /* strength in regs[0] */
        compile_expr(l, b);   /* confidence in regs[1] */
        bb_emit(b, OP_SET_TV);
    } else if (strcmp(head, "get-sti") == 0) {
        bb_emit(b, OP_GET_STI);
    } else if (strcmp(head, "set-sti") == 0) {
        compile_expr(l, b);
        bb_emit(b, OP_SET_STI);
    } else if (strcmp(head, "pln-ded") == 0) {
        compile_expr(l, b); compile_expr(l, b);
        compile_expr(l, b); compile_expr(l, b);
        bb_emit(b, OP_PLN_DED);
    } else if (strcmp(head, "pln-rev") == 0) {
        compile_expr(l, b); compile_expr(l, b);
        compile_expr(l, b); compile_expr(l, b);
        bb_emit(b, OP_PLN_REV);
    } else if (strcmp(head, "pln-abd") == 0) {
        for (int i = 0; i < 6; i++) compile_expr(l, b);
        bb_emit(b, OP_PLN_ABD);
    } else if (strcmp(head, "pln-ind") == 0) {
        for (int i = 0; i < 4; i++) compile_expr(l, b);
        bb_emit(b, OP_PLN_IND);
    } else if (strcmp(head, "pln-tmp") == 0) {
        for (int i = 0; i < 6; i++) compile_expr(l, b);
        bb_emit(b, OP_PLN_TMP);
    } else if (strcmp(head, "ecan-sp") == 0) {
        bb_emit(b, OP_ECAN_SP);
    } else if (strcmp(head, "send") == 0) {
        compile_expr(l, b);
        bb_emit(b, OP_SEND);
    } else if (strcmp(head, "recv") == 0) {
        bb_emit(b, OP_RECV);
    } else if (strcmp(head, "spawn") == 0) {
        bb_emit(b, OP_SPAWN);
    } else if (strcmp(head, "add") == 0) {
        compile_expr(l, b); compile_expr(l, b);
        bb_emit(b, OP_ADD);
    } else if (strcmp(head, "sub") == 0) {
        compile_expr(l, b); compile_expr(l, b);
        bb_emit(b, OP_SUB);
    } else if (strcmp(head, "mul") == 0) {
        compile_expr(l, b); compile_expr(l, b);
        bb_emit(b, OP_MUL);
    } else if (strcmp(head, "div") == 0) {
        compile_expr(l, b); compile_expr(l, b);
        bb_emit(b, OP_DIV);
    } else if (strcmp(head, "jmp") == 0) {
        /* (jmp <offset>) */
        char offs[32]; lex_atom(l, offs, sizeof(offs));
        uint64_t target = (uint64_t)strtoull(offs, NULL, 0);
        bb_emit(b, OP_JMP);
        bb_emit_u64(b, target);
    } else if (strcmp(head, "call") == 0) {
        char offs[32]; lex_atom(l, offs, sizeof(offs));
        uint64_t target = (uint64_t)strtoull(offs, NULL, 0);
        bb_emit(b, OP_CALL);
        bb_emit_u64(b, target);
    } else if (strcmp(head, "ret") == 0) {
        bb_emit(b, OP_RET);
    } else if (strcmp(head, "push") == 0) {
        bb_emit(b, OP_PUSH);
    } else if (strcmp(head, "pop") == 0) {
        bb_emit(b, OP_POP);
    } else {
        /* Unknown form — emit NOP */
        bb_emit(b, OP_NOP);
    }

    /* Consume closing ')' */
    lex_skip(l);
    if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
    return 0;
}

static int compile_expr(Lexer* l, ByteBuffer* b) {
    lex_skip(l);
    if (l->pos >= l->len) return 0;
    if (l->src[l->pos] == '(') {
        l->pos++;
        return compile_list(l, b);
    }
    /* Atom: just skip (value loading not yet supported at bytecode level) */
    char atom[64];
    lex_atom(l, atom, sizeof(atom));
    (void)atom;
    bb_emit(b, OP_NOP);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

ElmPackage* elbo_compile(const char* source, const char* type_name) {
    Lexer l = { source, 0, strlen(source) };
    ByteBuffer b = {0};

    /* Each top-level form becomes part of the on-message entry point.
     * We reserve a 1-byte preamble for the ep_on_gc HALT. */
    size_t ep_init       = 0;   /* empty init: just HALT */
    bb_emit(&b, OP_HALT);

    size_t ep_on_message = b.size;
    lex_skip(&l);
    while (l.pos < l.len) {
        compile_expr(&l, &b);
        lex_skip(&l);
    }
    bb_emit(&b, OP_HALT);

    size_t ep_on_gc = b.size;
    bb_emit(&b, OP_HALT);

    ElmPackage* pkg = calloc(1, sizeof(ElmPackage));
    if (!pkg) { free(b.buf); return NULL; }

    pkg->magic         = ELM_MAGIC;
    pkg->version       = 1;
    pkg->type_id       = cogdiod_hash_type(type_name);
    pkg->ep_init       = ep_init;
    pkg->ep_on_message = ep_on_message;
    pkg->ep_on_gc      = ep_on_gc;
    pkg->dis_bytecode  = b.buf;
    pkg->bytecode_size = b.size;
    pkg->ref_count     = 0;
    pkg->next_in_cache = NULL;
    strncpy(pkg->name, type_name, ELM_NAME_MAX - 1);
    pthread_mutex_init(&pkg->ref_lock, NULL);

    fprintf(stderr, "[elbo] compiled '%s' → %zu bytes\n",
            type_name, pkg->bytecode_size);
    return pkg;
}
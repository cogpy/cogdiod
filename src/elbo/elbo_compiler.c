/*
 * elbo_compiler.c — Minimal Elbo S-expr → Dis VM bytecode compiler
 *
 * Grammar (simplified):
 *   program     := (elbo-module TypeName form*)
 *   form        := (defun fn-name (args...) expr*)
 *   fn-name     := 'init' | 'on-message' | 'on-gc'
 *   expr        := atom-expr | call-expr
 *   atom-expr   := 'nop' | 'halt' | 'get-tv' | 'set-tv' | 'get-sti'
 *                | 'set-sti' | 'pln-ded' | 'pln-rev' | 'pln-abd'
 *                | 'pln-ind' | 'ecan-sp' | 'recv'
 *   call-expr   := '(' fn-name arg* ')'
 *   arg         := integer
 */

#include "cogdiod.h"
#include "elm_types.h"
#include "elbo_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Token types
 * ───────────────────────────────────────────────────────────────────────── */

typedef enum {
    TOK_LPAREN, TOK_RPAREN,
    TOK_SYMBOL, TOK_INT,
    TOK_EOF, TOK_ERROR,
} TokType;

typedef struct {
    TokType type;
    char    text[128];
    int64_t ival;
} Token;

/* ─────────────────────────────────────────────────────────────────────────
 * Lexer
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char* src;
    size_t      pos;
    size_t      len;
} Lexer;

static void lex_skip_ws(Lexer* l) {
    while (l->pos < l->len) {
        char c = l->src[l->pos];
        if (c == ';') {
            while (l->pos < l->len && l->src[l->pos] != '\n') l->pos++;
        } else if (isspace((unsigned char)c)) {
            l->pos++;
        } else {
            break;
        }
    }
}

static Token lex_next(Lexer* l) {
    lex_skip_ws(l);
    if (l->pos >= l->len)
        return (Token){ TOK_EOF, "", 0 };

    char c = l->src[l->pos];

    if (c == '(') { l->pos++; return (Token){ TOK_LPAREN, "(", 0 }; }
    if (c == ')') { l->pos++; return (Token){ TOK_RPAREN, ")", 0 }; }

    if (c == '-' || isdigit((unsigned char)c)) {
        size_t start = l->pos;
        if (c == '-') l->pos++;
        while (l->pos < l->len && isdigit((unsigned char)l->src[l->pos]))
            l->pos++;
        size_t slen = l->pos - start;
        if (slen >= 127) slen = 127;
        Token t = { TOK_INT, "", 0 };
        memcpy(t.text, l->src + start, slen);
        t.text[slen] = '\0';
        t.ival = strtoll(t.text, NULL, 10);
        return t;
    }

    /* Symbol */
    size_t start = l->pos;
    while (l->pos < l->len) {
        char ch = l->src[l->pos];
        if (ch == '(' || ch == ')' || isspace((unsigned char)ch)) break;
        l->pos++;
    }
    size_t slen = l->pos - start;
    if (slen >= 127) slen = 127;
    Token t = { TOK_SYMBOL, "", 0 };
    memcpy(t.text, l->src + start, slen);
    t.text[slen] = '\0';
    return t;
}

static Token lex_peek(Lexer* l) {
    size_t saved = l->pos;
    Token t = lex_next(l);
    l->pos = saved;
    return t;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Bytecode emitter
 * ───────────────────────────────────────────────────────────────────────── */

#define EMIT_CAP 4096

typedef struct {
    uint8_t* buf;
    size_t   len;
    size_t   cap;
} Emitter;

static void emit_init(Emitter* e) {
    e->buf = calloc(EMIT_CAP, 1);
    e->len = 0;
    e->cap = EMIT_CAP;
}

static void emit_byte(Emitter* e, uint8_t b) {
    if (e->len >= e->cap) {
        e->cap *= 2;
        e->buf = realloc(e->buf, e->cap);
    }
    e->buf[e->len++] = b;
}

static void emit_u64(Emitter* e, uint64_t v) {
    for (int i = 0; i < 8; i++)
        emit_byte(e, (uint8_t)((v >> (i * 8)) & 0xFF));
}

/* ─────────────────────────────────────────────────────────────────────────
 * Parser / compiler
 * ───────────────────────────────────────────────────────────────────────── */

/* Map keyword to opcode */
static int keyword_to_opcode(const char* kw, uint8_t* op) {
    static const struct { const char* kw; uint8_t op; } table[] = {
        { "nop",     OP_NOP     },
        { "halt",    OP_HALT    },
        { "get-tv",  OP_GET_TV  },
        { "set-tv",  OP_SET_TV  },
        { "get-sti", OP_GET_STI },
        { "set-sti", OP_SET_STI },
        { "pln-ded", OP_PLN_DED },
        { "pln-rev", OP_PLN_REV },
        { "pln-abd", OP_PLN_ABD },
        { "pln-ind", OP_PLN_IND },
        { "ecan-sp", OP_ECAN_SP },
        { "recv",    OP_RECV    },
        { "send",    OP_SEND    },
        { "spawn",   OP_SPAWN   },
        { NULL, 0 }
    };
    for (int i = 0; table[i].kw; i++) {
        if (strcmp(kw, table[i].kw) == 0) {
            *op = table[i].op;
            return 1;
        }
    }
    return 0;
}

static int compile_expr(Lexer* l, Emitter* e);

static int compile_call(Lexer* l, Emitter* e, const char* fn_name) {
    uint8_t op;
    if (keyword_to_opcode(fn_name, &op)) {
        emit_byte(e, op);
        Token t = lex_peek(l);
        /* For send/spawn, consume one integer argument */
        if ((op == OP_SEND || op == OP_SPAWN) && t.type == TOK_INT) {
            lex_next(l);
        }
        /* Consume RPAREN */
        t = lex_next(l);
        if (t.type != TOK_RPAREN) {
            fprintf(stderr, "[elbo] expected ')' after %s\n", fn_name);
            return -1;
        }
        return 0;
    }
    fprintf(stderr, "[elbo] unknown function: %s\n", fn_name);
    return -1;
}

static int compile_expr(Lexer* l, Emitter* e) {
    Token t = lex_next(l);

    if (t.type == TOK_EOF) return 0;

    if (t.type == TOK_SYMBOL) {
        uint8_t op;
        if (keyword_to_opcode(t.text, &op)) {
            emit_byte(e, op);
            return 0;
        }
        /* Ignore unknown symbols (e.g., parameter names) */
        return 0;
    }

    if (t.type == TOK_LPAREN) {
        Token head = lex_next(l);
        if (head.type == TOK_SYMBOL) {
            /* Recursively compile body expressions in a form */
            uint8_t op;
            if (keyword_to_opcode(head.text, &op)) {
                emit_byte(e, op);
                Token nxt = lex_next(l);
                if (nxt.type != TOK_RPAREN) {
                    fprintf(stderr, "[elbo] expected ')' after op %s\n",
                            head.text);
                    return -1;
                }
                return 0;
            }
            /* Nested form: skip to matching ) */
            int depth = 1;
            while (depth > 0) {
                Token s = lex_next(l);
                if (s.type == TOK_EOF) break;
                if (s.type == TOK_LPAREN)  depth++;
                if (s.type == TOK_RPAREN)  depth--;
            }
            return 0;
        }
        /* Skip unknown forms */
        int depth = 1;
        while (depth > 0) {
            Token s = lex_next(l);
            if (s.type == TOK_EOF) break;
            if (s.type == TOK_LPAREN)  depth++;
            if (s.type == TOK_RPAREN)  depth--;
        }
        return 0;
    }

    return 0;
}

/* Compile a (defun name (args) body...) → bytecode, return offset */
static size_t compile_defun(Lexer* l, Emitter* e) {
    size_t start_off = e->len;

    /* Consume arg list: (self ...) */
    Token t = lex_next(l);
    if (t.type == TOK_LPAREN) {
        int depth = 1;
        while (depth > 0) {
            t = lex_next(l);
            if (t.type == TOK_EOF) break;
            if (t.type == TOK_LPAREN) depth++;
            if (t.type == TOK_RPAREN) depth--;
        }
    }

    /* Compile body expressions */
    t = lex_peek(l);
    while (t.type != TOK_RPAREN && t.type != TOK_EOF) {
        if (compile_expr(l, e) != 0) break;
        t = lex_peek(l);
    }

    /* Always emit HALT at end of handler */
    emit_byte(e, OP_HALT);

    /* Consume closing ) of defun */
    if (t.type == TOK_RPAREN)
        lex_next(l);

    (void)compile_call;  /* suppress unused-function warning */
    return start_off;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

ElmPackage* elbo_compile(const char* source, const char* type_name_override) {
    Lexer l = { source, 0, strlen(source) };
    Emitter e;
    emit_init(&e);

    char type_name[ELM_NAME_MAX] = "ElboModule";
    uint64_t ep_init       = 0;
    uint64_t ep_on_message = 0;
    uint64_t ep_on_gc      = 0;
    bool has_init = false, has_msg = false, has_gc = false;

    /* Expect top-level (elbo-module TypeName ...) */
    Token t = lex_next(&l);
    if (t.type != TOK_LPAREN) {
        fprintf(stderr, "[elbo] expected '('\n");
        free(e.buf); return NULL;
    }

    t = lex_next(&l);
    if (t.type != TOK_SYMBOL || strcmp(t.text, "elbo-module") != 0) {
        fprintf(stderr, "[elbo] expected 'elbo-module'\n");
        free(e.buf); return NULL;
    }

    /* TypeName */
    t = lex_next(&l);
    if (t.type == TOK_SYMBOL)
        strncpy(type_name, t.text, ELM_NAME_MAX - 1);

    if (type_name_override)
        strncpy(type_name, type_name_override, ELM_NAME_MAX - 1);

    /* Parse (defun ...) forms */
    t = lex_peek(&l);
    while (t.type != TOK_RPAREN && t.type != TOK_EOF) {
        Token form_start = lex_next(&l);
        if (form_start.type != TOK_LPAREN) { t = lex_peek(&l); continue; }

        Token kw = lex_next(&l);
        if (kw.type != TOK_SYMBOL || strcmp(kw.text, "defun") != 0) {
            /* Skip unknown forms */
            int depth = 1;
            while (depth > 0) {
                Token s = lex_next(&l);
                if (s.type == TOK_EOF) break;
                if (s.type == TOK_LPAREN) depth++;
                if (s.type == TOK_RPAREN) depth--;
            }
            t = lex_peek(&l);
            continue;
        }

        Token fn_name = lex_next(&l);
        if (fn_name.type != TOK_SYMBOL) { t = lex_peek(&l); continue; }

        size_t off = compile_defun(&l, &e);

        if (strcmp(fn_name.text, "init") == 0) {
            ep_init = off; has_init = true;
        } else if (strcmp(fn_name.text, "on-message") == 0) {
            ep_on_message = off; has_msg = true;
        } else if (strcmp(fn_name.text, "on-gc") == 0) {
            ep_on_gc = off; has_gc = true;
        }

        t = lex_peek(&l);
    }

    /* If handlers not defined, emit stub HALTs */
    if (!has_init)       { ep_init       = e.len; emit_byte(&e, OP_HALT); }
    if (!has_msg)        { ep_on_message = e.len; emit_byte(&e, OP_HALT); }
    if (!has_gc)         { ep_on_gc      = e.len; emit_byte(&e, OP_HALT); }

    ElmPackage* pkg = calloc(1, sizeof(ElmPackage));
    if (!pkg) { free(e.buf); return NULL; }

    pkg->magic         = ELM_MAGIC;
    pkg->version       = 1;
    pkg->type_id       = cogdiod_hash_type(type_name);
    strncpy(pkg->name, type_name, ELM_NAME_MAX - 1);
    pkg->dis_bytecode  = e.buf;
    pkg->bytecode_size = e.len;
    pkg->ep_init       = ep_init;
    pkg->ep_on_message = ep_on_message;
    pkg->ep_on_gc      = ep_on_gc;
    pkg->ref_count     = 0;
    pkg->next_in_cache = NULL;
    pthread_mutex_init(&pkg->ref_lock, NULL);

    fprintf(stderr, "[elbo] compiled '%s' (%zu bytes, "
            "init=%llu msg=%llu gc=%llu)\n",
            type_name, e.len,
            (unsigned long long)ep_init,
            (unsigned long long)ep_on_message,
            (unsigned long long)ep_on_gc);
    return pkg;
}

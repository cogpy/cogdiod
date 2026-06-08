/*
 * elbo_compiler.c — Elbo S-expression → Dis bytecode compiler (Item 11)
 *
 * Grammar (extended for Phase 4):
 *   expr  ::= atom | number | '(' head expr* ')'
 *   head  ::= 'concept' | 'implication' | 'evaluation'
 *           | 'pln-ded' | 'pln-rev' | 'pln-abd' | 'pln-ind' | 'pln-tmp'
 *           | 'set-tv' | 'get-tv' | 'set-sti' | 'get-sti'
 *           | 'send' | 'recv' | 'spawn' | 'halt'
 *           | 'if' | 'cond' | 'when' | 'unless'      (Phase 4.2: conditionals)
 *           | 'let' | 'define'                        (Phase 4.1: bindings)
 *           | 'fn' | 'defun'                          (Phase 4.4: lambdas)
 *   number ::= [0-9]+\.?[0-9]* (Phase 4.5: float literals)
 */

#include "elbo_compiler.h"
#include "elm_types.h"
#include "cogdiod.h"
#include "elm_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

/* Maximum size for source files (1MB) */
#define ELBO_MAX_SOURCE_SIZE (1024 * 1024)

/* Maximum number of clauses in a cond expression */
#define ELBO_MAX_COND_CLAUSES 64

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

/* Phase 4.5: Emit float as 4-byte IEEE 754 (little-endian) */
static int bb_emit_float(ByteBuffer* b, float f) {
    if (bb_ensure(b, 4) != 0) return -1;
    union { float f; uint32_t u; } conv;
    conv.f = f;
    for (int i = 0; i < 4; i++)
        b->buf[b->size++] = (conv.u >> (i * 8)) & 0xFF;
    return 0;
}

/* Get current bytecode offset (for backpatching) */
static size_t bb_offset(ByteBuffer* b) {
    return b->size;
}

/* Backpatch a u64 target address at a given offset */
static void bb_patch_u64(ByteBuffer* b, size_t offset, uint64_t value) {
    for (int i = 0; i < 8; i++)
        b->buf[offset + i] = (value >> (i * 8)) & 0xFF;
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

/* Phase 4.5: Check if a token looks like a number (int or float) */
static int is_number(const char* s) {
    if (!s || !*s) return 0;
    /* Optional leading sign */
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    int has_digit = 0;
    while (isdigit((unsigned char)*s)) { s++; has_digit = 1; }
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) { s++; has_digit = 1; }
    }
    /* Optional exponent */
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-' || *s == '+') s++;
        while (isdigit((unsigned char)*s)) s++;
    }
    return has_digit && (*s == '\0');
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
    }
    /* ─────────────────────────────────────────────────────────────────────
     * Phase 4.2: Conditionals (if / cond / when / unless)
     * ───────────────────────────────────────────────────────────────────── */
    else if (strcmp(head, "if") == 0) {
        /* (if test then else)
         * Compile: test → JEQ else_label → then → JMP end_label → else → end
         */
        compile_expr(l, b);              /* compile test */
        bb_emit(b, OP_JEQ);              /* jump if false (regs[0] == 0) */
        size_t else_addr = bb_offset(b);
        bb_emit_u64(b, 0);               /* placeholder for else address */
        
        compile_expr(l, b);              /* compile then branch */
        bb_emit(b, OP_JMP);              /* jump to end */
        size_t end_addr = bb_offset(b);
        bb_emit_u64(b, 0);               /* placeholder for end address */
        
        bb_patch_u64(b, else_addr, bb_offset(b)); /* patch else address */
        compile_expr(l, b);              /* compile else branch */
        
        bb_patch_u64(b, end_addr, bb_offset(b)); /* patch end address */
    } else if (strcmp(head, "when") == 0) {
        /* (when test body...)
         * Like if with no else branch
         */
        compile_expr(l, b);              /* compile test */
        bb_emit(b, OP_JEQ);              /* jump if false */
        size_t end_addr = bb_offset(b);
        bb_emit_u64(b, 0);               /* placeholder */
        
        /* Compile body expressions until ')' */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
        
        bb_patch_u64(b, end_addr, bb_offset(b));
    } else if (strcmp(head, "unless") == 0) {
        /* (unless test body...)
         * Like when but inverted test
         */
        compile_expr(l, b);              /* compile test */
        bb_emit(b, OP_JNE);              /* jump if true (regs[0] != 0) */
        size_t end_addr = bb_offset(b);
        bb_emit_u64(b, 0);               /* placeholder */
        
        /* Compile body expressions until ')' */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
        
        bb_patch_u64(b, end_addr, bb_offset(b));
    } else if (strcmp(head, "cond") == 0) {
        /* (cond (test1 expr1) (test2 expr2) ... (else exprN))
         * Compiles to a chain of if-then-else
         */
        size_t jump_to_end[ELBO_MAX_COND_CLAUSES];
        int jump_count = 0;
        
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] == '(') {
            l->pos++; /* consume '(' */
            lex_skip(l);
            
            /* Save position to check for 'else' without consuming */
            size_t saved_pos = l->pos;
            char clause_head[64];
            lex_atom(l, clause_head, sizeof(clause_head));
            
            if (strcmp(clause_head, "else") == 0) {
                /* else clause: just compile the body */
                lex_skip(l);
                while (l->pos < l->len && l->src[l->pos] != ')') {
                    compile_expr(l, b);
                    lex_skip(l);
                }
                if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
                break;
            } else {
                /* Regular clause: restore position and compile test expr */
                l->pos = saved_pos;
                compile_expr(l, b);
                
                bb_emit(b, OP_JEQ);
                size_t next_clause = bb_offset(b);
                bb_emit_u64(b, 0);
                
                /* Compile body until ')' */
                lex_skip(l);
                while (l->pos < l->len && l->src[l->pos] != ')') {
                    compile_expr(l, b);
                    lex_skip(l);
                }
                
                /* Jump to end after body */
                if (jump_count < ELBO_MAX_COND_CLAUSES) {
                    bb_emit(b, OP_JMP);
                    jump_to_end[jump_count++] = bb_offset(b);
                    bb_emit_u64(b, 0);
                } else {
                    /* Clause limit exceeded - stop processing further clauses */
                    fprintf(stderr, "[elbo] error: cond exceeds %d clauses, skipping rest\n", 
                            ELBO_MAX_COND_CLAUSES);
                    if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
                    /* Skip all remaining clauses by consuming up to closing paren */
                    int depth = 1;
                    while (l->pos < l->len && depth > 0) {
                        if (l->src[l->pos] == '(') depth++;
                        else if (l->src[l->pos] == ')') depth--;
                        l->pos++;
                    }
                    break;
                }
                
                bb_patch_u64(b, next_clause, bb_offset(b));
            }
            
            if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
            lex_skip(l);
        }
        
        /* Patch all jumps to end */
        for (int i = 0; i < jump_count; i++) {
            bb_patch_u64(b, jump_to_end[i], bb_offset(b));
        }
    }
    /* ─────────────────────────────────────────────────────────────────────
     * Phase 4.1: let and define forms
     * ───────────────────────────────────────────────────────────────────── */
    else if (strcmp(head, "let") == 0) {
        /* (let ((x expr) (y expr2)) body...)
         * For stub: compile bindings, then body
         */
        lex_skip(l);
        if (l->pos < l->len && l->src[l->pos] == '(') {
            l->pos++; /* consume outer '(' for bindings */
            lex_skip(l);
            while (l->pos < l->len && l->src[l->pos] == '(') {
                l->pos++; /* consume binding '(' */
                char varname[64];
                lex_atom(l, varname, sizeof(varname));
                (void)varname; /* ignore name for now */
                compile_expr(l, b); /* compile value */
                bb_emit(b, OP_STORE); /* store to heap slot */
                lex_skip(l);
                if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
                lex_skip(l);
            }
            if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
        }
        /* Compile body */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
    } else if (strcmp(head, "define") == 0) {
        /* (define name expr) */
        char varname[64];
        lex_atom(l, varname, sizeof(varname));
        (void)varname;
        compile_expr(l, b);
        bb_emit(b, OP_STORE);
    }
    /* ─────────────────────────────────────────────────────────────────────
     * Phase 4.4: Lambda and function definitions
     * ───────────────────────────────────────────────────────────────────── */
    else if (strcmp(head, "fn") == 0 || strcmp(head, "lambda") == 0) {
        /* (fn (args...) body) - anonymous function
         * For stub: skip args, compile body with call/ret frame
         */
        lex_skip(l);
        if (l->pos < l->len && l->src[l->pos] == '(') {
            l->pos++;
            /* Skip argument list */
            while (l->pos < l->len && l->src[l->pos] != ')') {
                char arg[64];
                lex_atom(l, arg, sizeof(arg));
                (void)arg;
                lex_skip(l);
            }
            if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
        }
        /* Compile body */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
        bb_emit(b, OP_RET);
    } else if (strcmp(head, "defun") == 0) {
        /* (defun name (args...) body) */
        char funcname[64];
        lex_atom(l, funcname, sizeof(funcname));
        (void)funcname;
        
        /* Skip argument list */
        lex_skip(l);
        if (l->pos < l->len && l->src[l->pos] == '(') {
            l->pos++;
            while (l->pos < l->len && l->src[l->pos] != ')') {
                char arg[64];
                lex_atom(l, arg, sizeof(arg));
                (void)arg;
                lex_skip(l);
            }
            if (l->pos < l->len && l->src[l->pos] == ')') l->pos++;
        }
        /* Compile body */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
        bb_emit(b, OP_RET);
    } else if (strcmp(head, "elbo-module") == 0) {
        /* (elbo-module Name body...)
         * Skip module name, then compile all body forms
         */
        char modname[64];
        lex_atom(l, modname, sizeof(modname));
        (void)modname;
        
        /* Compile all nested forms until closing ')' */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
    } else {
        /* Unknown form — skip all sub-expressions and emit NOP */
        lex_skip(l);
        while (l->pos < l->len && l->src[l->pos] != ')') {
            compile_expr(l, b);
            lex_skip(l);
        }
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
    /* Atom: check if it's a number literal (Phase 4.5) */
    char atom[64];
    lex_atom(l, atom, sizeof(atom));
    
    if (is_number(atom)) {
        /* Emit LOAD opcode with float constant (use strtof for precision) */
        char* endptr;
        errno = 0;
        float f = strtof(atom, &endptr);
        if (errno != 0 || endptr == atom) {
            fprintf(stderr, "[elbo] warning: invalid float literal '%s'\n", atom);
            f = 0.0f;
        }
        bb_emit(b, OP_LOAD);
        bb_emit_float(b, f);
    } else {
        /* Symbol - emit NOP for now (would be a LOAD from symbol table) */
        bb_emit(b, OP_NOP);
    }
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

/* ─────────────────────────────────────────────────────────────────────────
 * Phase 4.6: File-based compilation (.elbo → .elm)
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * elbo_compile_file — Read source from a file, compile, and write .elm output.
 *
 * The type_name is derived from the source filename (basename without extension).
 */
int elbo_compile_file(const char* src_path, const char* out_path) {
    /* Read source file */
    FILE* f = fopen(src_path, "rb");
    if (!f) {
        fprintf(stderr, "[elbo] failed to open source: %s\n", src_path);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > ELBO_MAX_SOURCE_SIZE) {
        fclose(f);
        fprintf(stderr, "[elbo] invalid source file size: %ld (max: %d)\n", 
                fsize, ELBO_MAX_SOURCE_SIZE);
        return -1;
    }
    
    char* source = malloc((size_t)fsize + 1);
    if (!source) {
        fclose(f);
        return -1;
    }
    
    size_t read_bytes = fread(source, 1, (size_t)fsize, f);
    fclose(f);
    source[read_bytes] = '\0';
    
    /* Derive type name from filename */
    char type_name[ELM_NAME_MAX] = {0};
    const char* basename = src_path;
    const char* p = src_path;
    while (*p) {
        if (*p == '/' || *p == '\\') basename = p + 1;
        p++;
    }
    /* Copy basename, strip extension */
    size_t i = 0;
    while (basename[i] && basename[i] != '.' && i < ELM_NAME_MAX - 1) {
        type_name[i] = basename[i];
        i++;
    }
    type_name[i] = '\0';
    
    /* Convert underscores to CamelCase for type name */
    /* e.g., "concept_node" → "ConceptNode" */
    char camel_name[ELM_NAME_MAX] = {0};
    int camel_idx = 0;
    int cap_next = 1;
    for (size_t j = 0; type_name[j] && camel_idx < ELM_NAME_MAX - 1; j++) {
        if (type_name[j] == '_') {
            cap_next = 1;
        } else {
            if (cap_next) {
                camel_name[camel_idx++] = (char)toupper((unsigned char)type_name[j]);
            } else {
                camel_name[camel_idx++] = type_name[j];
            }
            cap_next = 0;
        }
    }
    camel_name[camel_idx] = '\0';
    
    /* Compile */
    ElmPackage* pkg = elbo_compile(source, camel_name);
    free(source);
    
    if (!pkg) {
        fprintf(stderr, "[elbo] compilation failed for %s\n", src_path);
        return -1;
    }
    
    /* Save to .elm file */
    int rc = elm_save(pkg, out_path);
    
    /* Cleanup */
    free(pkg->dis_bytecode);
    pthread_mutex_destroy(&pkg->ref_lock);
    free(pkg);
    
    if (rc != 0) {
        fprintf(stderr, "[elbo] failed to save %s\n", out_path);
        return -1;
    }
    
    fprintf(stderr, "[elbo] compiled %s → %s\n", src_path, out_path);
    return 0;
}
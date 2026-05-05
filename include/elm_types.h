/*
 * elm_types.h — Dis VM opcodes and Elbo package builder types
 *
 * Shared by elm_loader.c and all package stub builders.
 */

#pragma once

#include "cogdiod.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Cognitive opcode set (subset of Dis + CogDiod extensions)
 * ───────────────────────────────────────────────────────────────────────── */

typedef enum {
    /* Standard Dis arithmetic */
    OP_NOP    = 0x00,
    OP_ADD    = 0x01,
    OP_SUB    = 0x02,
    OP_MUL    = 0x03,
    OP_DIV    = 0x04,

    /* Control flow */
    OP_JMP    = 0x10,
    OP_JEQ    = 0x11,
    OP_JNE    = 0x12,
    OP_CALL   = 0x13,
    OP_RET    = 0x14,

    /* Memory */
    OP_LOAD   = 0x20,
    OP_STORE  = 0x21,
    OP_PUSH   = 0x22,
    OP_POP    = 0x23,

    /* Channel operations */
    OP_SEND   = 0x30,
    OP_RECV   = 0x31,
    OP_SPAWN  = 0x32,

    /* Cognitive operations */
    OP_GET_TV = 0x40,
    OP_SET_TV = 0x41,
    OP_GET_STI= 0x42,
    OP_SET_STI= 0x43,
    OP_PLN_DED= 0x44,
    OP_PLN_REV= 0x45,
    OP_ECAN_SP= 0x46,

    /* Extended PLN rules */
    OP_PLN_ABD= 0x50,
    OP_PLN_IND= 0x51,
    OP_PLN_TMP= 0x52,

    /* Halt */
    OP_HALT   = 0xFF,
} DisOpcode;

/* ─────────────────────────────────────────────────────────────────────────
 * Stub package definition (used by elm_build_stub)
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char*    type_name;
    const uint8_t* init_bc;   size_t init_bc_len;
    const uint8_t* msg_bc;    size_t msg_bc_len;
    const uint8_t* gc_bc;     size_t gc_bc_len;
} ElmStubDef;

/* ─────────────────────────────────────────────────────────────────────────
 * elm_loader.c public API
 * ───────────────────────────────────────────────────────────────────────── */

ElmPackage* elm_build_stub(const ElmStubDef* def);
int         elm_save(const ElmPackage* pkg, const char* path);
int         elm_exec_init(AtomIsolate* a);
int         elm_exec_msg(AtomIsolate* a, const CogMessage* msg);

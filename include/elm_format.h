/*
 * elm_format.h — Binary .elm package on-disk layout
 *
 * Layout:
 *   [ElmFileHeader]       — 128 bytes, fixed
 *   [bytecode section]    — header.bytecode_size bytes
 *   [symbol table]        — header.symtab_count × ElmSymEntry entries
 *
 * All multi-byte integers are little-endian.
 */
#pragma once
#include <stdint.h>

#define ELM_FILE_MAGIC    0x454C4D00u   /* "ELM\0" */
#define ELM_FILE_VERSION  2u
#define ELM_SYMNAME_MAX   64

/*
 * On-disk header — exactly 128 bytes.
 * Size check: 4*4 + 64 + 8*3 + 4*2 = 16+64+24+8 = 112 → pad 16 → 128
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* ELM_FILE_MAGIC */
    uint32_t version;         /* ELM_FILE_VERSION */
    uint32_t type_id;         /* djb2 hash of type_name */
    uint32_t flags;           /* reserved, must be 0 */
    char     type_name[64];   /* null-terminated type name */
    uint64_t ep_init;         /* bytecode offset: (init) entry point */
    uint64_t ep_on_message;   /* bytecode offset: (on-message) entry point */
    uint64_t ep_on_gc;        /* bytecode offset: (on-gc) entry point */
    uint32_t bytecode_size;   /* size of bytecode section in bytes */
    uint32_t symtab_count;    /* number of symbol table entries */
    uint8_t  _pad[16];        /* padding to reach 128 bytes */
} ElmFileHeader;

/* Symbol table entry */
typedef struct __attribute__((packed)) {
    char     name[ELM_SYMNAME_MAX];  /* symbol name */
    uint64_t offset;                 /* bytecode offset */
    uint32_t flags;                  /* 0=function, 1=data */
    uint32_t _pad;
} ElmSymEntry;

_Static_assert(sizeof(ElmFileHeader) == 128, "ElmFileHeader must be 128 bytes");

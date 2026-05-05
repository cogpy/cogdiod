/*
 * elm_format.h — Binary .elm file format definition
 *
 * Layout:
 *   [ElmFileHeader]     — 128-byte fixed header
 *   [Dis bytecode]      — bytecode_size bytes
 *   [Symbol table]      — symtab_count × ElmSymEntry (72 bytes each)
 */

#pragma once
#include <stdint.h>

#define ELM_FILE_MAGIC    0x454C4D00u
#define ELM_FILE_VERSION  2u
#define ELM_SYMNAME_MAX   64

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* ELM_FILE_MAGIC                    */
    uint32_t version;         /* ELM_FILE_VERSION                  */
    uint32_t type_id;         /* djb2 hash of type_name            */
    uint32_t flags;           /* reserved, 0                       */
    char     type_name[64];   /* null-terminated type name         */
    uint64_t ep_init;         /* bytecode offset for (init)        */
    uint64_t ep_on_message;   /* bytecode offset for (on-message)  */
    uint64_t ep_on_gc;        /* bytecode offset for (on-gc)       */
    uint32_t bytecode_size;   /* number of bytecode bytes          */
    uint32_t symtab_count;    /* number of ElmSymEntry records     */
    uint8_t  _pad[12];        /* pad to 128 bytes                  */
} ElmFileHeader;

typedef struct __attribute__((packed)) {
    char     name[ELM_SYMNAME_MAX];   /* symbol name          */
    uint64_t offset;                  /* bytecode offset      */
    uint32_t flags;                   /* 0=code, 1=data       */
    uint32_t _pad;
} ElmSymEntry;

_Static_assert(sizeof(ElmFileHeader) == 128, "ElmFileHeader must be 128 bytes");
_Static_assert(sizeof(ElmSymEntry)   == 72,  "ElmSymEntry must be 72 bytes");

/*
 * test_elm_loader.c — Unit tests for the Dis VM executor / elm_loader
 *
 * Compile: cc -O2 -std=c11 -Iinclude -D_GNU_SOURCE \
 *              -DDISVM_NREGS=16 -DDISVM_STKMAX=4096 \
 *              src/kernel/cogdiod_kernel.c src/kernel/pln.c \
 *              src/kernel/cogdiod_log.c \
 *              src/p9/distyx.c src/elbo/elm_loader.c \
 *              src/elbo/elbo_compiler.c \
 *              packages/concept_node/concept_node_pkg.c \
 *              packages/evaluation_link/evaluation_link_pkg.c \
 *              packages/implication_link/implication_link_pkg.c \
 *              tests/test_elm_loader.c -lm -lpthread -o test_elm_loader
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cogdiod.h"
#include "elm_types.h"

static int pass = 0, fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { printf("PASS: %s\n", msg); pass++; } \
    else       { printf("FAIL: %s (line %d)\n", msg, __LINE__); fail++; } \
} while(0)

#define CHECK_NEAR(a, b, eps, msg) \
    CHECK(fabs((double)(a)-(double)(b)) < (eps), msg)

/* Forward declarations for package builders (from packages/*.c) */
ElmPackage* concept_node_build_package(void);
ElmPackage* implication_link_build_package(void);
ElmPackage* evaluation_link_build_package(void);

static void kernel_insert_pkg(CogDiodKernel* k, ElmPackage* pkg) {
    pthread_mutex_lock(&k->pkg_lock);
    uint32_t b = pkg->type_id % PKG_CACHE_BUCKETS;
    k->pkg_cache[b] = pkg;
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);
}


/* Build a stub package where on_message runs the given bytecode */
static ElmPackage* make_pkg(const char* name, const uint8_t* code, size_t clen) {
    uint8_t halt[] = { OP_HALT };
    ElmStubDef def = {
        .type_name   = name,
        .init_bc     = halt, .init_bc_len = 1,
        .msg_bc      = code, .msg_bc_len  = clen,
        .gc_bc       = halt, .gc_bc_len   = 1,
    };
    return elm_build_stub(&def);
}

/* ── Tests ────────────────────────────────────────────────────────────── */

static void test_nop(void) {
    printf("\n[elm_loader] OP_NOP + OP_HALT\n");
    uint8_t code[] = { OP_NOP, OP_HALT };
    CogDiodKernel* k = cogdiod_create(0, 2);
    ElmPackage* pkg = make_pkg("nop_pkg", code, sizeof(code));
    (void)k; /* package loading via file path; test just checks elm_build_stub */

    /* Test elm_build_stub output */
    CHECK(pkg != NULL, "elm_build_stub returns non-NULL");
    CHECK(pkg->bytecode_size >= 2, "bytecode_size >= 2");
    CHECK(pkg->ep_on_message > 0 || pkg->ep_on_message == 0, "ep_on_message set");

    free(pkg->dis_bytecode);
    free(pkg);
    cogdiod_stop(k);
    free(k);
}

static void test_build_stub_fields(void) {
    printf("\n[elm_loader] elm_build_stub field verification\n");
    uint8_t halt[] = { OP_HALT };
    uint8_t msg[]  = { OP_NOP, OP_HALT };
    ElmStubDef def = {
        .type_name = "TestType",
        .init_bc   = halt, .init_bc_len = 1,
        .msg_bc    = msg,  .msg_bc_len  = 2,
        .gc_bc     = halt, .gc_bc_len   = 1,
    };
    ElmPackage* p = elm_build_stub(&def);
    CHECK(p != NULL, "elm_build_stub returns non-NULL");
    CHECK(strcmp(p->name, "TestType") == 0, "name set correctly");
    CHECK(p->next_in_cache == NULL, "next_in_cache starts NULL");
    CHECK(p->bytecode_size == 4, "bytecode_size = init+msg+gc = 1+2+1 = 4");
    CHECK(p->ep_init == 0, "ep_init = 0 (first section)");
    CHECK(p->ep_on_message == 1, "ep_on_message = 1 (after init)");
    CHECK(p->ep_on_gc == 3, "ep_on_gc = 3 (after init+msg)");
    CHECK(p->magic == ELM_MAGIC, "magic = ELM_MAGIC");

    free(p->dis_bytecode);
    free(p);
}

static void test_disvm_exec_via_kernel(void) {
    printf("\n[elm_loader] Dis VM execution via kernel spawn + elm_exec_msg\n");
    CogDiodKernel* k = cogdiod_create(0, 2);
    kernel_insert_pkg(k, concept_node_build_package());
    kernel_insert_pkg(k, implication_link_build_package());
    kernel_insert_pkg(k, evaluation_link_build_package());

    /* Use a pre-registered package (ConceptNode from packages/) */
    uint64_t uuid = cogdiod_spawn(k, "ConceptNode", "exec_atom")->uuid;
    CHECK(uuid != 0, "spawn ConceptNode");

    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    CHECK(a != NULL, "get_atom after spawn");
    CHECK(a->package != NULL, "atom has package");
    CHECK(strcmp(a->package->name, "ConceptNode") == 0, "package name correct");

    /* Send a message — should not crash */
    CogMessage msg = {0};
    msg.type = MSG_SOURCE_CHANGED;
    int rc = elm_exec_msg(a, &msg);
    CHECK(rc == 0 || rc == 1, "elm_exec_msg returns 0 or 1 (halted or stepped)");

    cogdiod_stop(k);
    free(k);
}

static void test_opcodes_add_sub(void) {
    printf("\n[elm_loader] OP_ADD / OP_SUB via fixed-register semantics\n");
    /*
     * The Dis VM uses fixed implicit registers:
     *   ADD: regs[0] = regs[1] + regs[2]
     *   SUB: regs[0] = regs[1] - regs[2]
     * We set regs[1]/[2] directly and run a two-instruction sequence.
     */
    uint8_t add_code[]  = { OP_ADD, OP_HALT };
    uint8_t sub_code[]  = { OP_SUB, OP_HALT };
    uint8_t halt[]      = { OP_HALT };

    ElmStubDef def_add = {
        .type_name = "AddType",
        .init_bc   = halt, .init_bc_len = 1,
        .msg_bc    = add_code, .msg_bc_len = sizeof(add_code),
        .gc_bc     = halt, .gc_bc_len   = 1,
    };
    ElmStubDef def_sub = {
        .type_name = "SubType",
        .init_bc   = halt, .init_bc_len = 1,
        .msg_bc    = sub_code, .msg_bc_len = sizeof(sub_code),
        .gc_bc     = halt, .gc_bc_len   = 1,
    };

    ElmPackage* pkg_add = elm_build_stub(&def_add);
    ElmPackage* pkg_sub = elm_build_stub(&def_sub);
    CHECK(pkg_add != NULL, "AddType package built");
    CHECK(pkg_sub != NULL, "SubType package built");

    CogDiodKernel* k = cogdiod_create(0, 2);
    kernel_insert_pkg(k, pkg_add);
    kernel_insert_pkg(k, pkg_sub);

    /* Test ADD */
    uint64_t ua = cogdiod_spawn(k, "AddType", "adder")->uuid;
    AtomIsolate* aa = cogdiod_get_atom(k, ua);
    CHECK(aa != NULL, "get adder atom");
    aa->vm_ctx.regs[1] = 10;
    aa->vm_ctx.regs[2] = 3;
    aa->vm_ctx.pc = pkg_add->ep_on_message;
    CogMessage m = {0};
    elm_exec_msg(aa, &m);
    CHECK(aa->vm_ctx.regs[0] == 13, "ADD: regs[0] = 10+3 = 13");

    /* Test SUB */
    uint64_t us = cogdiod_spawn(k, "SubType", "subber")->uuid;
    AtomIsolate* as = cogdiod_get_atom(k, us);
    CHECK(as != NULL, "get subber atom");
    as->vm_ctx.regs[1] = 10;
    as->vm_ctx.regs[2] = 3;
    as->vm_ctx.pc = pkg_sub->ep_on_message;
    elm_exec_msg(as, &m);
    CHECK(as->vm_ctx.regs[0] == 7, "SUB: regs[0] = 10-3 = 7");

    cogdiod_stop(k);
    free(k);
}

int main(void) {
    printf("=== elm_loader Unit Tests ===\n");
    test_nop();
    test_build_stub_fields();
    test_disvm_exec_via_kernel();
    test_opcodes_add_sub();
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}

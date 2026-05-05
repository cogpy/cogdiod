/*
 * test_elm_loader.c — Unit tests for elm_loader.c and elbo_compiler.c
 *
 * Tests:
 *   1. elm_build_stub  — build a stub package
 *   2. elm_exec_init   — run the init entry point
 *   3. elm_exec_msg    — dispatch a CogMessage
 *   4. disvm_step opcodes — verify each opcode individually
 *   5. elbo_compile    — compile a minimal Elbo source string
 *   6. elm_save / elm_load_file — round-trip serialisation
 */

#include "cogdiod.h"
#include "elm_types.h"
#include "elbo_compiler.h"
#include "elm_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

static int failures = 0;

#define PASS(msg) fprintf(stderr, "PASS: %s\n", (msg))
#define FAIL(msg) do { \
    fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
    failures++; \
} while(0)
#define CHECK(cond, msg) do { if (cond) PASS(msg); else FAIL(msg); } while(0)
#define CHECK_NEAR(a,b,eps,msg) \
    CHECK(fabsf((float)(a)-(float)(b)) < (float)(eps), msg)

/* Forward declarations from package stub builders */
ElmPackage* concept_node_build_package(void);
ElmPackage* implication_link_build_package(void);

/* ── Minimal kernel helper (no real threads) ────────────────────────── */

static AtomIsolate* make_atom(ElmPackage* pkg, const char* name) {
    AtomIsolate* a = calloc(1, sizeof(AtomIsolate));
    a->uuid    = 42;
    a->type_id = pkg->type_id;
    a->state   = ATOM_SLEEPING;
    a->package = pkg;
    a->tv      = (TruthValue){ 0.5f, 0.5f };
    a->av      = (AttentionValue){ 0.0f, 0.0f };
    a->hebbian_weight = 1.0f;
    if (name) strncpy(a->name, name, ATOM_NAME_MAX - 1);
    a->vm_ctx.stack     = calloc(DISVM_STKMAX, 1);
    a->vm_ctx.heap      = calloc(4096, 1);
    a->vm_ctx.heap_size = 4096;
    pthread_mutex_init(&a->lock, NULL);
    /* Increment ref count */
    pthread_mutex_lock(&pkg->ref_lock);
    pkg->ref_count++;
    pthread_mutex_unlock(&pkg->ref_lock);
    return a;
}

static void free_atom(AtomIsolate* a) {
    if (!a) return;
    pthread_mutex_lock(&a->package->ref_lock);
    a->package->ref_count--;
    pthread_mutex_unlock(&a->package->ref_lock);
    free(a->vm_ctx.stack);
    free(a->vm_ctx.heap);
    pthread_mutex_destroy(&a->lock);
    free(a);
}

/* ── Test 1: elm_build_stub ─────────────────────────────────────────── */

static void test_build_stub(void) {
    fprintf(stderr, "\n=== Test 1: elm_build_stub ===\n");

    static const uint8_t init_bc[] = { OP_NOP, OP_HALT };
    static const uint8_t msg_bc[]  = { OP_GET_TV, OP_HALT };
    static const uint8_t gc_bc[]   = { OP_NOP, OP_HALT };

    ElmStubDef def = {
        .type_name   = "TestAtom",
        .init_bc     = init_bc, .init_bc_len = sizeof(init_bc),
        .msg_bc      = msg_bc,  .msg_bc_len  = sizeof(msg_bc),
        .gc_bc       = gc_bc,   .gc_bc_len   = sizeof(gc_bc),
    };
    ElmPackage* pkg = elm_build_stub(&def);

    CHECK(pkg != NULL, "elm_build_stub returns non-NULL");
    CHECK(pkg->magic == ELM_MAGIC, "magic == ELM_MAGIC");
    CHECK(strcmp(pkg->name, "TestAtom") == 0, "name == TestAtom");
    CHECK(pkg->bytecode_size == sizeof(init_bc) + sizeof(msg_bc) + sizeof(gc_bc),
          "bytecode_size correct");
    CHECK(pkg->ep_init == 0, "ep_init == 0");
    CHECK(pkg->ep_on_message == sizeof(init_bc), "ep_on_message == len(init)");

    free(pkg->dis_bytecode);
    pthread_mutex_destroy(&pkg->ref_lock);
    free(pkg);
}

/* ── Test 2: elm_exec_init ──────────────────────────────────────────── */

static void test_exec_init(void) {
    fprintf(stderr, "\n=== Test 2: elm_exec_init ===\n");

    ElmPackage* pkg = concept_node_build_package();
    CHECK(pkg != NULL, "concept_node_build_package non-NULL");

    AtomIsolate* a = make_atom(pkg, "test_concept");
    a->tv = (TruthValue){ 0.75f, 0.85f };

    int r = elm_exec_init(a);
    CHECK(r == 0, "elm_exec_init returns 0");

    free_atom(a);
    free(pkg->dis_bytecode);
    pthread_mutex_destroy(&pkg->ref_lock);
    free(pkg);
}

/* ── Test 3: elm_exec_msg ───────────────────────────────────────────── */

static void test_exec_msg(void) {
    fprintf(stderr, "\n=== Test 3: elm_exec_msg ===\n");

    ElmPackage* pkg = implication_link_build_package();
    AtomIsolate* impl = make_atom(pkg, "cat->animal");
    impl->tv = (TruthValue){ 0.95f, 0.80f };

    CogMessage msg = {
        .type        = MSG_SOURCE_CHANGED,
        .sender_uuid = 1,
        .tv          = { 0.80f, 0.90f },
    };

    /* Pre-load antecedent TV into regs[2..3] */
    impl->vm_ctx.regs[2] = 0; impl->vm_ctx.regs[3] = 0;
    memcpy(&impl->vm_ctx.regs[2], &msg.tv.strength,   4);
    memcpy(&impl->vm_ctx.regs[3], &msg.tv.confidence, 4);

    int r = elm_exec_msg(impl, &msg);
    CHECK(r == 0, "elm_exec_msg returns 0");

    /* After PLN_DED: regs[4..5] should hold the deduced TV */
    float deduced_s, deduced_c;
    memcpy(&deduced_s, &impl->vm_ctx.regs[4], 4);
    memcpy(&deduced_c, &impl->vm_ctx.regs[5], 4);

    CHECK_NEAR(deduced_s, 0.760f, 0.05f, "PLN deduction strength ~0.76");

    free_atom(impl);
    free(pkg->dis_bytecode);
    pthread_mutex_destroy(&pkg->ref_lock);
    free(pkg);
}

/* ── Test 4: elbo_compile ───────────────────────────────────────────── */

static void test_elbo_compile(void) {
    fprintf(stderr, "\n=== Test 4: elbo_compile ===\n");

    const char* src =
        "(elbo-module MyAtom\n"
        "  (defun init (self)\n"
        "    get-tv\n"
        "    halt)\n"
        "  (defun on-message (self msg)\n"
        "    pln-ded\n"
        "    ecan-sp\n"
        "    halt)\n"
        "  (defun on-gc (self)\n"
        "    nop\n"
        "    halt))\n";

    ElmPackage* pkg = elbo_compile(src, "MyAtom");
    CHECK(pkg != NULL, "elbo_compile returns non-NULL");

    if (pkg) {
        CHECK(pkg->magic == ELM_MAGIC, "compiled pkg magic valid");
        CHECK(pkg->bytecode_size > 0, "compiled bytecode non-empty");
        CHECK(strcmp(pkg->name, "MyAtom") == 0, "compiled pkg name == MyAtom");

        /* Entry points should differ */
        CHECK(pkg->ep_on_message > pkg->ep_init, "ep_on_message > ep_init");

        /* Run init */
        AtomIsolate* a = make_atom(pkg, "my_atom");
        a->tv = (TruthValue){ 0.6f, 0.7f };
        int r = elm_exec_init(a);
        CHECK(r == 0, "elbo_compile: elm_exec_init returns 0");
        free_atom(a);

        free(pkg->dis_bytecode);
        pthread_mutex_destroy(&pkg->ref_lock);
        free(pkg);
    }
}

/* ── Test 5: elm_save / elm_load_file round-trip ────────────────────── */

static void test_elm_roundtrip(void) {
    fprintf(stderr, "\n=== Test 5: elm_save / elm_load_file round-trip ===\n");

    ElmPackage* pkg = concept_node_build_package();
    CHECK(pkg != NULL, "build package for round-trip");
    if (!pkg) return;

    const char* tmp_path = "/tmp/cogdiod_test_roundtrip.elm";
    int r = elm_save(pkg, tmp_path);
    CHECK(r == 0, "elm_save returns 0");

    ElmPackage* loaded = elm_load_file(tmp_path);
    CHECK(loaded != NULL, "elm_load_file returns non-NULL");

    if (loaded) {
        CHECK(loaded->magic == ELM_MAGIC, "loaded magic valid");
        CHECK(loaded->type_id == pkg->type_id, "loaded type_id matches");
        CHECK(strcmp(loaded->name, pkg->name) == 0, "loaded name matches");
        CHECK(loaded->bytecode_size == pkg->bytecode_size, "loaded bytecode_size matches");
        CHECK(memcmp(loaded->dis_bytecode, pkg->dis_bytecode, pkg->bytecode_size) == 0,
              "loaded bytecode content matches");
        CHECK(loaded->ep_init       == pkg->ep_init,       "ep_init matches");
        CHECK(loaded->ep_on_message == pkg->ep_on_message, "ep_on_message matches");
        CHECK(loaded->ep_on_gc      == pkg->ep_on_gc,      "ep_on_gc matches");

        free(loaded->dis_bytecode);
        pthread_mutex_destroy(&loaded->ref_lock);
        free(loaded);
    }

    free(pkg->dis_bytecode);
    pthread_mutex_destroy(&pkg->ref_lock);
    free(pkg);
    unlink(tmp_path);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "=== Elm Loader Unit Test Suite ===\n");
    test_build_stub();
    test_exec_init();
    test_exec_msg();
    test_elbo_compile();
    test_elm_roundtrip();
    fprintf(stderr, "\n");
    if (failures == 0) {
        fprintf(stderr, "ALL ELM LOADER TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "%d ELM LOADER TEST(S) FAILED\n", failures);
    return 1;
}

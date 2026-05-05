/*
 * test_distyx.c — Unit tests for the DisTyx protocol layer
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
 *              tests/test_distyx.c -lm -lpthread -o test_distyx
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cogdiod.h"
#include "distyx.h"

static int pass = 0, fail = 0;

#define CHECK(expr, msg) do { \
    if (expr) { printf("PASS: %s\n", msg); pass++; } \
    else       { printf("FAIL: %s (line %d)\n", msg, __LINE__); fail++; } \
} while(0)

#define CHECK_NEAR(a, b, eps, msg) \
    CHECK(fabs((double)(a)-(double)(b)) < (eps), msg)

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* Package builders from packages/*.c */
ElmPackage* concept_node_build_package(void);
ElmPackage* implication_link_build_package(void);
ElmPackage* evaluation_link_build_package(void);

static void insert_pkg(CogDiodKernel* k, ElmPackage* pkg) {
    pthread_mutex_lock(&k->pkg_lock);
    k->pkg_cache[pkg->type_id % PKG_CACHE_BUCKETS] = pkg;
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);
}

static CogDiodKernel* make_kernel(void) {
    CogDiodKernel* k = cogdiod_create(0, 2);
    insert_pkg(k, concept_node_build_package());
    insert_pkg(k, implication_link_build_package());
    insert_pkg(k, evaluation_link_build_package());
    return k;
}

static DisTyxRequest req_create(int op, const char* path) {
    DisTyxRequest r = {0};
    r.op = op;
    strncpy(r.path, path, sizeof(r.path)-1);
    return r;
}

/* ── Tests ────────────────────────────────────────────────────────────── */

static void test_create_atom(void) {
    printf("\n[distyx] CREATE /ai/atoms/<type>/<name>\n");
    CogDiodKernel* k = make_kernel();

    DisTyxRequest  req  = req_create(DT_OP_CREATE, "/ai/atoms/ConceptNode/cat");
    DisTyxResponse resp = {0};

    int rc = distyx_dispatch(k, &req, &resp);
    CHECK(rc == 0, "CREATE returns 0");
    CHECK(resp.buf_len == 8, "CREATE returns 8-byte UUID");

    uint64_t uuid = 0;
    memcpy(&uuid, resp.buf, 8);
    CHECK(uuid != 0, "UUID is non-zero");

    AtomIsolate* a = cogdiod_get_atom(k, uuid);
    CHECK(a != NULL, "atom found in kernel after CREATE");

    cogdiod_stop(k);
    free(k);
}

static void test_read_write_tv(void) {
    printf("\n[distyx] READ/WRITE /ai/atoms/<uuid>/tv\n");
    CogDiodKernel* k = make_kernel();

    /* Create an atom */
    uint64_t uuid = cogdiod_spawn(k, "ConceptNode", "dog")->uuid;
    CHECK(uuid != 0, "spawn dog atom");

    /* WRITE tv */
    char path[128];
    snprintf(path, sizeof(path), "/ai/atoms/%llu/tv", (unsigned long long)uuid);
    DisTyxRequest wreq = req_create(DT_OP_WRITE, path);
    float tv_buf[2] = { 0.75f, 0.85f };
    memcpy(wreq.buf, tv_buf, 8);
    wreq.buf_len = 8;
    DisTyxResponse wresp = {0};
    int rc = distyx_dispatch(k, &wreq, &wresp);
    CHECK(rc == 0, "WRITE tv returns 0");

    /* READ tv */
    DisTyxRequest rreq = req_create(DT_OP_READ, path);
    DisTyxResponse rresp = {0};
    rc = distyx_dispatch(k, &rreq, &rresp);
    CHECK(rc == 0, "READ tv returns 0");
    CHECK(rresp.buf_len == 8, "READ tv returns 8 bytes");

    float got[2];
    memcpy(got, rresp.buf, 8);
    CHECK_NEAR(got[0], 0.75f, 0.001f, "READ tv: strength = 0.75");
    CHECK_NEAR(got[1], 0.85f, 0.001f, "READ tv: confidence = 0.85");

    cogdiod_stop(k);
    free(k);
}

static void test_stat(void) {
    printf("\n[distyx] STAT /ai/atoms/<uuid>\n");
    CogDiodKernel* k = make_kernel();

    uint64_t uuid = cogdiod_spawn(k, "ConceptNode", "tree")->uuid;
    CHECK(uuid != 0, "spawn tree");

    char path[128];
    snprintf(path, sizeof(path), "/ai/atoms/%llu", (unsigned long long)uuid);
    DisTyxRequest req = req_create(DT_OP_STAT, path);
    DisTyxResponse resp = {0};
    int rc = distyx_dispatch(k, &req, &resp);
    CHECK(rc == 0, "STAT returns 0");
    CHECK(resp.buf_len > 0, "STAT has output");

    /* JSON should contain "uuid" */
    resp.buf[resp.buf_len] = '\0';
    CHECK(strstr((char*)resp.buf, "\"uuid\"") != NULL, "STAT JSON has uuid");
    CHECK(strstr((char*)resp.buf, "\"tv\"") != NULL, "STAT JSON has tv");

    cogdiod_stop(k);
    free(k);
}

static void test_list_atoms(void) {
    printf("\n[distyx] READ /ai/atoms/  (list)\n");
    CogDiodKernel* k = make_kernel();

    cogdiod_spawn(k, "ConceptNode", "a1")->uuid;
    cogdiod_spawn(k, "ConceptNode", "a2")->uuid;

    DisTyxRequest req = req_create(DT_OP_READ, "/ai/atoms/");
    DisTyxResponse resp = {0};
    int rc = distyx_dispatch(k, &req, &resp);
    CHECK(rc == 0, "list atoms returns 0");
    CHECK(resp.buf_len > 2, "list non-empty");

    resp.buf[resp.buf_len] = '\0';
    CHECK(resp.buf[0] == '[', "list starts with [");

    cogdiod_stop(k);
    free(k);
}

static void test_remove_atom(void) {
    printf("\n[distyx] REMOVE /ai/atoms/<uuid>\n");
    CogDiodKernel* k = make_kernel();

    uint64_t uuid = cogdiod_spawn(k, "ConceptNode", "gone")->uuid;
    CHECK(uuid != 0, "spawn gone");

    char path[128];
    snprintf(path, sizeof(path), "/ai/atoms/%llu", (unsigned long long)uuid);
    DisTyxRequest req = req_create(DT_OP_REMOVE, path);
    DisTyxResponse resp = {0};
    int rc = distyx_dispatch(k, &req, &resp);
    CHECK(rc == 0, "REMOVE returns 0");
    CHECK(cogdiod_get_atom(k, uuid) == NULL, "atom gone from pool");

    cogdiod_stop(k);
    free(k);
}

static void test_stats(void) {
    printf("\n[distyx] READ /ai/stats\n");
    CogDiodKernel* k = make_kernel();
    cogdiod_spawn(k, "ConceptNode", "s1")->uuid;

    DisTyxRequest req = req_create(DT_OP_READ, "/ai/stats");
    DisTyxResponse resp = {0};
    int rc = distyx_dispatch(k, &req, &resp);
    CHECK(rc == 0, "stats returns 0");
    CHECK(resp.buf_len > 0, "stats non-empty");
    resp.buf[resp.buf_len] = '\0';
    CHECK(strstr((char*)resp.buf, "\"atoms\"") != NULL, "stats has atoms key");

    cogdiod_stop(k);
    free(k);
}

int main(void) {
    printf("=== DisTyx Unit Tests ===\n");
    test_create_atom();
    test_read_write_tv();
    test_stat();
    test_list_atoms();
    test_remove_atom();
    test_stats();
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}

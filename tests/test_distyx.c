/*
 * test_distyx.c — Unit tests for distyx.c path dispatch
 *
 * Tests:
 *   1. distyx_parse_path — parse various path strings
 *   2. distyx_dispatch   — /ai/stats
 *   3. distyx_dispatch   — /ai/atoms/ listing
 *   4. distyx_dispatch   — /ai/atoms/<uuid>/tv read/write
 *   5. distyx_dispatch   — /ai/atoms/<uuid>/lti read
 *   6. distyx_dispatch   — /ai/atoms/<uuid>/type read
 *   7. distyx_dispatch   — /ai/atoms/<uuid>/links read
 *   8. distyx_dispatch   — STAT /ai/atoms/<uuid>
 *   9. distyx_dispatch   — unknown path returns error
 */

#include "cogdiod.h"
#include "distyx.h"
#include "elm_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;

#define PASS(msg)  fprintf(stderr, "PASS: %s\n", (msg))
#define FAIL(msg)  do { fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); failures++; } while(0)
#define CHECK(c,m) do { if (c) PASS(m); else FAIL(m); } while(0)

/* Package builder forward decls */
ElmPackage* concept_node_build_package(void);

static void kernel_insert_package(CogDiodKernel* k, ElmPackage* pkg) {
    uint32_t b = pkg->type_id % PKG_CACHE_BUCKETS;
    pthread_mutex_lock(&k->pkg_lock);
    /* Chain into bucket */
    pkg->next_in_cache = k->pkg_cache[b];
    k->pkg_cache[b] = pkg;
    k->pkg_count++;
    pthread_mutex_unlock(&k->pkg_lock);
}

static CogDiodKernel* make_test_kernel(void) {
    CogDiodKernel* k = cogdiod_create(0, 0);
    ElmPackage* cn_pkg = concept_node_build_package();
    kernel_insert_package(k, cn_pkg);
    cogdiod_start(k);
    return k;
}

/* ── Test 1: stats path ─────────────────────────────────────────────── */
static void test_stats(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 1: /ai/stats ===\n");
    DisTyxRequest  req  = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/stats");

    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "stats dispatch returns 0");
    CHECK(resp.buf_len > 0, "stats response non-empty");
    CHECK(strstr((char*)resp.buf, "atoms") != NULL, "stats JSON contains 'atoms'");
    CHECK(strstr((char*)resp.buf, "workers") != NULL, "stats JSON contains 'workers'");
}

/* ── Test 2: atom listing ────────────────────────────────────────────── */
static void test_list_atoms(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 2: /ai/atoms/ list ===\n");
    /* Spawn a couple of atoms first */
    cogdiod_spawn(k, "ConceptNode", "alpha");
    cogdiod_spawn(k, "ConceptNode", "beta");

    DisTyxRequest  req  = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/atoms/");

    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "list atoms dispatch returns 0");
    CHECK(resp.buf_len > 2, "list atoms response non-trivial (>2 bytes)");
    CHECK(resp.buf[0] == '[', "list atoms starts with '['");
}

/* ── Test 3: TV read / write ─────────────────────────────────────────── */
static void test_tv_rw(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 3: TV read/write ===\n");
    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "gamma");
    CHECK(a != NULL, "spawn gamma non-NULL");
    if (!a) return;

    /* Write TV */
    DisTyxRequest  wreq  = { .op = DT_OP_WRITE };
    DisTyxResponse wresp = {0};
    snprintf(wreq.path, sizeof(wreq.path), "/ai/atoms/%llu/tv",
             (unsigned long long)a->uuid);
    float ws = 0.77f, wc = 0.88f;
    memcpy(wreq.buf,     &ws, 4);
    memcpy(wreq.buf + 4, &wc, 4);
    wreq.buf_len = 8;
    int r = distyx_dispatch(k, &wreq, &wresp);
    CHECK(r == 0, "TV write returns 0");

    /* Read TV back */
    DisTyxRequest  rreq  = { .op = DT_OP_READ };
    DisTyxResponse rresp = {0};
    snprintf(rreq.path, sizeof(rreq.path), "/ai/atoms/%llu/tv",
             (unsigned long long)a->uuid);
    r = distyx_dispatch(k, &rreq, &rresp);
    CHECK(r == 0, "TV read returns 0");
    CHECK(rresp.buf_len == 8, "TV read returns 8 bytes");

    float rs, rc;
    memcpy(&rs, rresp.buf,     4);
    memcpy(&rc, rresp.buf + 4, 4);
    CHECK(fabsf(rs - ws) < 0.01f, "TV read strength matches written value");
    CHECK(fabsf(rc - wc) < 0.01f, "TV read confidence matches written value");
}

/* ── Test 4: LTI read ────────────────────────────────────────────────── */
static void test_lti_read(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 4: LTI read ===\n");
    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "delta");
    if (!a) { FAIL("spawn delta"); return; }

    DisTyxRequest  req  = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/atoms/%llu/lti",
             (unsigned long long)a->uuid);
    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "LTI read returns 0");
    CHECK(resp.buf_len == 4, "LTI read returns 4 bytes");
}

/* ── Test 5: type read ───────────────────────────────────────────────── */
static void test_type_read(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 5: type read ===\n");
    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "epsilon");
    if (!a) { FAIL("spawn epsilon"); return; }

    DisTyxRequest  req  = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/atoms/%llu/type",
             (unsigned long long)a->uuid);
    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "type read returns 0");
    CHECK(resp.buf_len > 0, "type read non-empty");
    CHECK(strncmp((char*)resp.buf, "ConceptNode", 11) == 0, "type read returns 'ConceptNode'");
}

/* ── Test 6: links read ──────────────────────────────────────────────── */
static void test_links_read(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 6: links read ===\n");
    AtomIsolate* src = cogdiod_spawn(k, "ConceptNode", "src_link_test");
    AtomIsolate* dst = cogdiod_spawn(k, "ConceptNode", "dst_link_test");
    if (!src || !dst) { FAIL("spawn src/dst"); return; }

    cogdiod_link(k, src->uuid, dst->uuid);

    DisTyxRequest  req  = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/atoms/%llu/links",
             (unsigned long long)src->uuid);
    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "links read returns 0");
    CHECK(resp.buf_len > 2, "links read non-trivial");
    CHECK(resp.buf[0] == '[', "links read starts with '['");
}

/* ── Test 7: STAT ────────────────────────────────────────────────────── */
static void test_stat(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 7: STAT atom ===\n");
    AtomIsolate* a = cogdiod_spawn(k, "ConceptNode", "zeta");
    if (!a) { FAIL("spawn zeta"); return; }

    DisTyxRequest  req  = { .op = DT_OP_STAT };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/atoms/%llu",
             (unsigned long long)a->uuid);
    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r == 0, "STAT returns 0");
    CHECK(resp.buf_len > 0, "STAT response non-empty");
    CHECK(strstr((char*)resp.buf, "uuid") != NULL, "STAT JSON contains 'uuid'");
    CHECK(strstr((char*)resp.buf, "ConceptNode") != NULL, "STAT JSON contains 'ConceptNode'");
}

/* ── Test 8: unknown path → error ────────────────────────────────────── */
static void test_unknown_path(CogDiodKernel* k) {
    fprintf(stderr, "\n=== Test 8: unknown path ===\n");
    DisTyxRequest  req  = { .op = DT_OP_READ };
    DisTyxResponse resp = {0};
    snprintf(req.path, sizeof(req.path), "/ai/nonexistent/path");
    int r = distyx_dispatch(k, &req, &resp);
    CHECK(r != 0, "unknown path returns non-zero");
    CHECK(resp.status != 0, "unknown path sets status != 0");
    CHECK(strlen(resp.errmsg) > 0, "unknown path sets errmsg");
}

/* ── main ─────────────────────────────────────────────────────────────── */
int main(void) {
    fprintf(stderr, "=== DisTyx Unit Test Suite ===\n");

    CogDiodKernel* k = make_test_kernel();

    test_stats(k);
    test_list_atoms(k);
    test_tv_rw(k);
    test_lti_read(k);
    test_type_read(k);
    test_links_read(k);
    test_stat(k);
    test_unknown_path(k);

    cogdiod_destroy(k);

    fprintf(stderr, "\n");
    if (failures == 0) {
        fprintf(stderr, "ALL DISTYX TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "%d DISTYX TEST(S) FAILED\n", failures);
    return 1;
}

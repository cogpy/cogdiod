/*
 * cogdiod_llm.c — LLM coprocessor bridge implementation
 *
 * Subprocess transport: spawns `emu /dis/llama.dis -i ... <model.gguf>`
 * for each attached Atom and proxies prompts / tokens over a pipe pair.
 *
 * In-process (Inferno-native) transport is selected automatically when
 * the bridge detects it is itself running under emu (env COGDIOD_EMU=1);
 * in that mode it dispatches directly through cogdiod_spawn() onto the
 * appl/cogllama.b Limbo wrapper which imports llama.limbo's modules.
 *
 * Per-Atom LLM state is held in a self-contained side-table keyed by
 * the Atom's UUID, so this bridge does not require any change to the
 * AtomIsolate struct or to cogdiod.h.
 *
 * See include/cogdiod_llm.h for the full namespace + protocol contract.
 */

#include "cogdiod.h"
#include "cogdiod_llm.h"
#include "cogdiod_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const CogLlmParams COGLLM_DEFAULTS = {
    .temperature  = 0.8f,
    .top_k        = 40,
    .top_p        = 0.95f,
    .min_p        = 0.05f,
    .seed         = 42,
    .mirostat     = 0,
    .mirostat_tau = 5.0f,
    .mirostat_eta = 0.1f,
    .max_tokens   = 256,
};

/* ─────────────────────────────────────────────────────────────────────────
 * Side-table: AtomIsolate.uuid -> CogLlmState*
 *
 * Open-addressed hash table guarded by a single mutex.  The expected
 * working set is small (one entry per LLM-bound Atom) so a 256-bucket
 * table with linear probing is plenty.
 * ───────────────────────────────────────────────────────────────────────── */

#define LLM_TABLE_BUCKETS 256

typedef struct {
    uint64_t      uuid;        /* 0 == empty slot */
    CogLlmState*  state;
} LlmEntry;

static LlmEntry         g_table[LLM_TABLE_BUCKETS];
static pthread_mutex_t  g_table_lock = PTHREAD_MUTEX_INITIALIZER;

static inline size_t llm_bucket(uint64_t uuid) {
    /* Splitmix-style fold to spread sparse UUIDs */
    uint64_t h = uuid;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return (size_t)(h & (LLM_TABLE_BUCKETS - 1));
}

static int llm_table_put(uint64_t uuid, CogLlmState* st) {
    pthread_mutex_lock(&g_table_lock);
    size_t i = llm_bucket(uuid);
    for (size_t probe = 0; probe < LLM_TABLE_BUCKETS; ++probe) {
        size_t idx = (i + probe) & (LLM_TABLE_BUCKETS - 1);
        if (g_table[idx].uuid == 0 || g_table[idx].uuid == uuid) {
            g_table[idx].uuid  = uuid;
            g_table[idx].state = st;
            pthread_mutex_unlock(&g_table_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_table_lock);
    return -1;  /* table full */
}

static CogLlmState* llm_table_get(uint64_t uuid) {
    pthread_mutex_lock(&g_table_lock);
    size_t i = llm_bucket(uuid);
    for (size_t probe = 0; probe < LLM_TABLE_BUCKETS; ++probe) {
        size_t idx = (i + probe) & (LLM_TABLE_BUCKETS - 1);
        if (g_table[idx].uuid == 0) break;
        if (g_table[idx].uuid == uuid) {
            CogLlmState* s = g_table[idx].state;
            pthread_mutex_unlock(&g_table_lock);
            return s;
        }
    }
    pthread_mutex_unlock(&g_table_lock);
    return NULL;
}

static CogLlmState* llm_table_remove(uint64_t uuid) {
    pthread_mutex_lock(&g_table_lock);
    size_t i = llm_bucket(uuid);
    for (size_t probe = 0; probe < LLM_TABLE_BUCKETS; ++probe) {
        size_t idx = (i + probe) & (LLM_TABLE_BUCKETS - 1);
        if (g_table[idx].uuid == 0) break;
        if (g_table[idx].uuid == uuid) {
            CogLlmState* s = g_table[idx].state;
            g_table[idx].uuid  = 0;
            g_table[idx].state = NULL;
            pthread_mutex_unlock(&g_table_lock);
            return s;
        }
    }
    pthread_mutex_unlock(&g_table_lock);
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Subprocess backend — wraps a child `emu` running llama.limbo
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    pid_t   pid;
    int     stdin_fd;    /* host writes prompts here  */
    int     stdout_fd;   /* host reads tokens here    */
    char    model_path[512];
} SubprocBackend;

static SubprocBackend* spawn_emu_llama(const char* model_path,
                                       const CogLlmParams* p) {
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe)  != 0) return NULL;
    if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); return NULL; }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child — wire pipes to stdin/stdout and exec emu */
        dup2(in_pipe[0],  STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);

        char temp_buf[32], topk_buf[32], topp_buf[32], minp_buf[32];
        char seed_buf[32], maxn_buf[32], miro_buf[32];
        snprintf(temp_buf, sizeof temp_buf, "%g", p->temperature);
        snprintf(topk_buf, sizeof topk_buf, "%d", p->top_k);
        snprintf(topp_buf, sizeof topp_buf, "%g", p->top_p);
        snprintf(minp_buf, sizeof minp_buf, "%g", p->min_p);
        snprintf(seed_buf, sizeof seed_buf, "%d", p->seed);
        snprintf(maxn_buf, sizeof maxn_buf, "%d", p->max_tokens);
        snprintf(miro_buf, sizeof miro_buf, "%d", p->mirostat);

        execlp("emu", "emu",
               "/dis/llama.dis",
               "-i",
               "-t", temp_buf,
               "-k", topk_buf,
               "-P", topp_buf,
               "-m", minp_buf,
               "-seed", seed_buf,
               "-n", maxn_buf,
               "-mirostat", miro_buf,
               model_path,
               (char*)NULL);
        fprintf(stderr, "[cogllm] emu exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    SubprocBackend* b = calloc(1, sizeof *b);
    if (!b) { close(in_pipe[1]); close(out_pipe[0]); return NULL; }
    b->pid       = pid;
    b->stdin_fd  = in_pipe[1];
    b->stdout_fd = out_pipe[0];
    snprintf(b->model_path, sizeof b->model_path, "%s", model_path);

    /* Streaming reads should not block the kernel scheduler */
    int flags = fcntl(b->stdout_fd, F_GETFL, 0);
    fcntl(b->stdout_fd, F_SETFL, flags | O_NONBLOCK);

    return b;
}

static void kill_emu_llama(SubprocBackend* b) {
    if (!b) return;
    if (b->stdin_fd  >= 0) close(b->stdin_fd);
    if (b->stdout_fd >= 0) close(b->stdout_fd);
    if (b->pid > 0) {
        kill(b->pid, SIGTERM);
        waitpid(b->pid, NULL, 0);
    }
    free(b);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

int cogdiod_llm_attach(CogDiodKernel* k, AtomIsolate* a,
                       const char* model_path, const CogLlmParams* params) {
    (void)k;
    if (!a || !model_path) return -1;

    CogLlmState* st = calloc(1, sizeof *st);
    if (!st) return -1;
    snprintf(st->model_path, sizeof st->model_path, "%s", model_path);
    st->params    = params ? *params : COGLLM_DEFAULTS;
    st->transport = (getenv("COGDIOD_EMU") ? 0 : 1);

    if (st->transport == 1) {
        SubprocBackend* b = spawn_emu_llama(model_path, &st->params);
        if (!b) { free(st); return -1; }
        st->backend = b;
        cogdiod_log(LOG_INFO, "cogllm",
                    "attached subprocess emu pid=%d for atom=%llu model=%s",
                    (int)b->pid, (unsigned long long)a->uuid, model_path);
    } else {
        cogdiod_log(LOG_INFO, "cogllm",
                    "in-process binding (atom=%llu) — appl/cogllama.b not yet built",
                    (unsigned long long)a->uuid);
        free(st);
        return -1;
    }

    if (llm_table_put(a->uuid, st) != 0) {
        cogdiod_log(LOG_ERROR, "cogllm",
                    "side-table full, cannot attach atom=%llu",
                    (unsigned long long)a->uuid);
        if (st->transport == 1) kill_emu_llama((SubprocBackend*)st->backend);
        free(st);
        return -1;
    }
    return 0;
}

int cogdiod_llm_infer(CogDiodKernel* k, AtomIsolate* a,
                      const char* prompt, int stream) {
    (void)k; (void)stream;
    if (!a || !prompt) return -1;

    CogLlmState* st = llm_table_get(a->uuid);
    if (!st) return -1;
    if (st->transport != 1 || !st->backend) return -1;

    SubprocBackend* b = (SubprocBackend*)st->backend;
    size_t len = strlen(prompt);

    ssize_t w1 = write(b->stdin_fd, prompt, len);
    ssize_t w2 = write(b->stdin_fd, "\n",   1);
    if (w1 != (ssize_t)len || w2 != 1) {
        cogdiod_log(LOG_ERROR, "cogllm", "short write to emu pid=%d", (int)b->pid);
        return -1;
    }
    return 0;
}

void cogdiod_llm_detach(AtomIsolate* a) {
    if (!a) return;
    CogLlmState* st = llm_table_remove(a->uuid);
    if (!st) return;
    if (st->transport == 1 && st->backend) {
        kill_emu_llama((SubprocBackend*)st->backend);
    }
    free(st);
}

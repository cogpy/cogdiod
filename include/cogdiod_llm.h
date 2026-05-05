/*
 * cogdiod_llm.h — LLM coprocessor bridge (cogpy/llama.limbo integration)
 *
 * Wires CogDiod AtomIsolates to the pure-Limbo LLaMA inference engine
 * provided by https://github.com/cogpy/llama.limbo.  Inference is
 * dispatched by sending an MSG_LLM_INFER message to an Atom of type
 * "llm-coprocessor", which then runs the request through a Limbo
 * `Llama` module instance via the existing OP_SPAWN infrastructure.
 *
 * Two transport modes are supported:
 *
 *   1.  IN-PROCESS  (Inferno-native)
 *       The CogDiod kernel is itself running under emu/infernos with
 *       llama.limbo's .dis files installed in /dis/lib/llama/.  An
 *       Atom of type "llm-coprocessor" is bound to a Limbo wrapper
 *       (appl/cogllama.b) that imports llama.limbo's modules directly.
 *       Inference messages are passed through the standard LimboChannel
 *       ring buffer with zero copy.
 *
 *   2.  SUBPROCESS  (POSIX host fallback)
 *       When CogDiod runs natively on Linux/POSIX (no Inferno host),
 *       the bridge spawns a child `emu` process running the
 *       llama.limbo command and proxies prompts/responses over a
 *       UNIX domain socket pair.  Per-Atom isolation is preserved;
 *       each Atom owns its own emu child.
 *
 * 9P namespace (DisTyx):
 *
 *   /ai/llm/<atom-uuid>/model     [W] GGUF model path (Twrite once at init)
 *   /ai/llm/<atom-uuid>/prompt    [W] Twrite a prompt → triggers inference
 *   /ai/llm/<atom-uuid>/response  [R] Tread to consume the next token (stream)
 *   /ai/llm/<atom-uuid>/params    [RW] JSON: temp, top_k, top_p, min_p, seed, mirostat
 *   /ai/llm/<atom-uuid>/stats     [R] tokens/sec, total tokens, model metadata
 *
 * Message protocol:
 *
 *   MSG_LLM_INFER   tagged CogMessage with .blob_size / .blob_offset
 *   MSG_LLM_TOKEN   single token reply (emitted per-token in stream mode)
 *   MSG_LLM_DONE    end-of-stream marker (with stats payload)
 */

#ifndef COGDIOD_LLM_H
#define COGDIOD_LLM_H

#include "cogdiod.h"
#include <stdint.h>

/* Extended message types (start at 0x10 to leave room for future core types) */
#define MSG_LLM_INFER   0x10
#define MSG_LLM_TOKEN   0x11
#define MSG_LLM_DONE    0x12

/* Sampling parameters mirror llama.limbo/module/sampler.m */
typedef struct {
    float    temperature;   /* 0.0 = greedy, 0.8 = default                 */
    int32_t  top_k;         /* 40 default; 0 = no top-k filter             */
    float    top_p;         /* 0.95 default; 1.0 = no nucleus filter       */
    float    min_p;         /* 0.05 default; 0.0 = no min-p filter         */
    int32_t  seed;          /* xorshift32 seed; 42 default                 */
    int32_t  mirostat;      /* 0=off, 1=v1, 2=v2                           */
    float    mirostat_tau;  /* target perplexity (mirostat v1/v2)          */
    float    mirostat_eta;  /* learning rate (mirostat v1/v2)              */
    int32_t  max_tokens;    /* 256 default                                 */
} CogLlmParams;

/* Per-Atom LLM state (allocated in AtomIsolate.user_state) */
typedef struct {
    char            model_path[512];
    CogLlmParams    params;
    void*           backend;       /* opaque: in-proc Limbo handle or subprocess pid+fd */
    int32_t         transport;     /* 0=in-process, 1=subprocess */
    uint64_t        tokens_emitted;
    uint64_t        ns_total;
} CogLlmState;

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

/* Bind an existing AtomIsolate as an LLM coprocessor.  Loads the model
 * eagerly and validates the GGUF header.  Returns 0 on success. */
int  cogdiod_llm_attach(CogDiodKernel* k, AtomIsolate* a,
                        const char* model_path, const CogLlmParams* params);

/* Submit a prompt for inference.  In stream mode the caller will receive
 * one MSG_LLM_TOKEN per generated token followed by an MSG_LLM_DONE.
 * Returns 0 on success, -1 on backend error. */
int  cogdiod_llm_infer(CogDiodKernel* k, AtomIsolate* a,
                       const char* prompt, int stream);

/* Detach and free LLM resources for an Atom (called from MSG_DESTROY). */
void cogdiod_llm_detach(AtomIsolate* a);

/* Default sampling parameters (matches llama.limbo CLI defaults). */
extern const CogLlmParams COGLLM_DEFAULTS;

#endif /* COGDIOD_LLM_H */

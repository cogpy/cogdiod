# cogdiod-llama — LLM coprocessor for CogDiod

This subdirectory wires [`cogpy/llama.limbo`](https://github.com/cogpy/llama.limbo) — a pure-Limbo LLaMA inference engine — into the CogDiod cognitive daemon as a first-class Atom type.

## Architecture

```
         ┌────────────────────────────────────────────────────┐
         │                   CogDiod kernel                   │
         │                                                    │
         │   AtomIsolatePool ─── AtomIsolate                  │
         │                       (type=llm-coprocessor)       │
         │                       │                            │
         │                       │ MSG_LLM_INFER              │
         │                       ▼                            │
         │                  LimboChannel                      │
         │                       │                            │
         └───────────────────────┼────────────────────────────┘
                                 │
                  ┌──────────────┴───────────────┐
                  ▼                              ▼
       ┌──────────────────────┐       ┌──────────────────────┐
       │  IN-PROCESS          │       │  SUBPROCESS          │
       │  (Inferno-native)    │       │  (POSIX host)        │
       │                      │       │                      │
       │  cogllama.b          │       │  fork() + execlp()   │
       │   ↓ imports          │       │   ↓                  │
       │  llama.limbo Llama   │       │  emu /dis/llama.dis  │
       └──────────────────────┘       │   -i <model.gguf>    │
                                      └──────────────────────┘
```

## Files

| File | Role |
|------|------|
| `../include/cogdiod_llm.h` | Public C API: `cogdiod_llm_attach()`, `cogdiod_llm_infer()`, `cogdiod_llm_detach()`, message types `MSG_LLM_INFER/TOKEN/DONE`, `CogLlmParams` |
| `../src/llm/cogdiod_llm.c` | C implementation. Side-table keyed by Atom UUID, subprocess transport via `emu`, in-process transport stub for Inferno-native runs |
| `appl/cogllama.b` | Limbo wrapper (in-process transport). Imports `llama.limbo`'s `Llama` and `Sampler` modules, adapts them to the CogDiod `CogMessage` protocol |

## 9P Namespace (DisTyx)

LLM coprocessor Atoms expose this namespace via DisTyx:

| Path | Mode | Semantics |
|------|------|-----------|
| `/ai/llm/<atom-uuid>/model` | W | Twrite GGUF model path (one-shot, at attach time) |
| `/ai/llm/<atom-uuid>/prompt` | W | Twrite a prompt → triggers inference |
| `/ai/llm/<atom-uuid>/response` | R | Tread to consume next token (streaming) |
| `/ai/llm/<atom-uuid>/params` | RW | JSON-encoded `CogLlmParams` |
| `/ai/llm/<atom-uuid>/stats` | R | tokens/sec, total tokens, model metadata |

## Quickstart

### Subprocess transport (POSIX host)

```c
#include "cogdiod.h"
#include "cogdiod_llm.h"

int main(void) {
    CogDiodKernel* k = cogdiod_init();

    AtomIsolate* a = cogdiod_spawn(k, "llm-coprocessor", "llama32-1b");
    cogdiod_llm_attach(k, a, "/models/llama3.2-1b-q4_0.gguf", &COGLLM_DEFAULTS);

    cogdiod_llm_infer(k, a, "The capital of France is", /*stream=*/1);

    /* …drain tokens via /ai/llm/<a->uuid>/response… */

    cogdiod_llm_detach(a);
    cogdiod_shutdown(k);
}
```

### In-process transport (Inferno-native)

When CogDiod runs under `emu` with `COGDIOD_EMU=1`:

```
; emu -r/inferno
; cogdiod &
; mount -bc {cogdiod-cli} /n/cogdiod
; echo /models/llama3.2-1b-q4_0.gguf > /n/cogdiod/ai/llm/llama32-1b/model
; echo "Hello from CogDiod" > /n/cogdiod/ai/llm/llama32-1b/prompt
; cat /n/cogdiod/ai/llm/llama32-1b/response
```

## Sampling parameters

`CogLlmParams` mirrors `llama.limbo/module/sampler.m`:

| Field | Default | Notes |
|-------|---------|-------|
| `temperature` | 0.8 | 0.0 = greedy |
| `top_k` | 40 | 0 = no filter |
| `top_p` | 0.95 | nucleus sampling |
| `min_p` | 0.05 | min-probability filter |
| `seed` | 42 | xorshift32 PRNG |
| `mirostat` | 0 | 0=off, 1=v1, 2=v2 |
| `mirostat_tau` | 5.0 | target perplexity |
| `mirostat_eta` | 0.1 | learning rate |
| `max_tokens` | 256 | generation limit |

## Quantisation support

Inherited from `llama.limbo`: F32, F16, BF16, Q4_0, Q4_1, Q5_0, Q5_1, Q8_0, Q8_1.

## Build

```
make llm                # builds src/llm/cogdiod_llm.o into the kernel
make llm-test           # spawns emu + a tiny test model, exercises attach/infer/detach
```

## License

Same as CogDiod (see top-level LICENSE). `llama.limbo` is a separate repo with its own license.

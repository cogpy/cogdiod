# CogDiod

**CogDiod** is an experimental cognitive architecture where each OpenCog AtomSpace Atom is an [Inferno](https://inferno-os.org/) [Dis VM](https://en.wikipedia.org/wiki/Dis_virtual_machine) isolate — a live, concurrent execution context — rather than a passive data record.

> "The knowledge *is* the program. Each Atom is simultaneously a datum and a process."

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    CogDiod Kernel                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │ConceptNode│  │Implication│  │Evaluation│  ...         │
│  │ Dis VM   │  │  Dis VM  │  │  Dis VM  │              │
│  │ isolate  │  │ isolate  │  │ isolate  │              │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘              │
│       │  Limbo typed channels      │                     │
│       └────────────┬───────────────┘                     │
│              DisTyx / 9P                                  │
│         /ai/atoms/{uuid}/tv                               │
│         /ai/atoms/{uuid}/sti                              │
│         /ai/stats                                         │
└─────────────────────────────────────────────────────────┘
```

### The Isolate Model

CogDiod follows the same pattern as V8 Isolates (Node.js/Cloudflare Workers), ATen execution contexts (PyTorch), and Erlang processes — **shared bytecode, isolated state**:

| Runtime | Isolate Unit | Shared Resource | Protocol |
| :--- | :--- | :--- | :--- |
| Node.js / workerd | V8 Isolate | JS bytecode snapshots | HTTP / fetch |
| PyTorch | ATen context | CUDA/CPU kernels | Tensor dispatch |
| **CogDiod** | **Dis VM Isolate** | **Limbo `.dis` bytecode** | **9P / DisTyx** |

Each Atom type (`ConceptNode`, `ImplicationLink`, etc.) is an **Elbo `.elm` package** — a Limbo bytecode module loaded once and instantiated per Atom. The Atom's execution frame, register state, and local value store are per-instance; the bytecode is shared.

---

## Repository Structure

```
cogdiod/                    # Core C kernel
├── include/
│   ├── cogdiod.h           # Core types: AtomIsolate, ElmPackage, CogDiodKernel
│   ├── elm_types.h         # Dis VM opcodes, ElmStubDef
│   └── distyx.h            # DisTyx 9P protocol types
├── src/
│   ├── kernel/
│   │   └── cogdiod_kernel.c  # Kernel: spawn, link, set_tv, PLN deduction
│   ├── p9/
│   │   └── distyx.c          # DisTyx: 9P filesystem over UNIX socket
│   └── elbo/
│       └── elm_loader.c      # Elbo package loader and builder
├── packages/
│   ├── concept_node/         # ConceptNode .elm package
│   ├── evaluation_link/      # EvaluationLink .elm package
│   └── implication_link/     # ImplicationLink .elm package
└── tests/
    └── test_cogdiod.c        # Integration test suite (22 assertions, all PASS)

cogdiod-lang/               # C bridge server for language integrations
└── bridge/
    ├── cogdiod_bridge.h
    └── cogdiod_bridge.c      # UNIX socket JSON bridge

cogdiod-native/             # Five native language integrations
├── maude/
│   ├── cogdiod.maude         # ★ Native Maude source (reflective term rewriting)
│   └── run_maude.clj         # Clojure executor (Maude semantics)
├── perl/
│   └── cogdiod.pl            # ★ Native Perl 5.34 (context overloading + AUTOLOAD)
├── racket/
│   ├── cogdiod.rkt           # ★ Native Racket source (#lang elbo dialect tower)
│   └── cogdiod_hy.hy         # Hy executor (Racket macro semantics)
├── clojure/
│   └── cogdiod.clj           # ★ Native Clojure (defprotocol + persistent AtomSpace)
└── guile/
    ├── cogdiod.scm           # ★ Native Guile Scheme (first-class environments)
    └── cogdiod_hy.hy         # Hy executor (Scheme environment semantics)
```

---

## Five Language Superpowers

### 1. Maude — Reflective Term Rewriting (`cogdiod.maude`)

Maude's `META-LEVEL` module allows rules to rewrite other rules. In CogDiod, this means PLN inference rules can **learn and rewrite themselves** based on observed performance:

```maude
mod COGDIOD-META is
  protecting COGDIOD-PLN .

  --- Meta-rule: if deduction has been applied 5+ times with stable results,
  --- boost the formula (learning as differential term rewriting)
  crl [boost-deduction] :
    rule(N, pln-deduction, K, TV) => rule(N, pln-boosted(1.15), K, TV)
    if K >= 5 .
endm
```

**Result:** After 5 applications, `pln-deduction → pln-boosted(1.15)`, lifting `P(animal)` from `TV(0.760, 0.648)` to `TV(0.874, 0.648)`.

### 2. Perl — Context-Sensitive Atoms + AUTOLOAD (`cogdiod.pl`)

Perl's `use overload` and `AUTOLOAD` make every Atom context-polymorphic and open to runtime extension:

```perl
use overload
    '""'  => \&as_string,   # "ConceptNode('cat') TV=(0.80,0.90)"
    '0+'  => \&as_number,   # STI value (numeric context)
    '@{}' => \&as_list,     # linked atom UUIDs (array context)
    'bool'=> \&as_bool;     # strength > 0.5 (boolean context)

# ecan_spread was never defined — AUTOLOAD generates it from the name
$cat->ecan_spread(2.5);     # works: STI 0.72 -> 3.22
```

### 3. Racket — `#lang elbo` Dialect Tower (`cogdiod.rkt`)

Racket's `#lang` system lets you define entire new languages as macro towers. CogDiod gets domain-specific cognitive languages with **zero runtime overhead**:

```racket
#lang elbo/temporal

; This is NOT a function call — it is a syntax transformation.
; (before A B c) compiles at read time to a bridge spawn call.
(before cat-eats animal-runs 0.75)   ; => ImplicationLink TV=(0.750, 0.712)
(during cat-eats feeding-time 0.90)  ; => EvaluationLink  TV=(0.900, 0.810)
```

### 4. Clojure — Protocols + Persistent AtomSpace (`cogdiod.clj`)

Clojure's `defprotocol` / `extend-protocol` solves the Expression Problem for Atoms. Persistent data structures give episodic memory for free:

```clojure
; Add Visualizable to AtomRecord WITHOUT modifying AtomRecord
(extend-protocol Visualizable
  AtomRecord
  (visualize [atom]
    (let [bar (Math/round (* (:s (get-tv atom)) 20))]
      (str (:type atom) " [" (apply str (repeat bar "█")) "] s=" (:s (get-tv atom))))))

; Episodic memory: every version costs O(log n), not O(n)
; v0 → v1 → v2 → v3 — all queryable, none mutated
```

### 5. Guile Scheme — First-Class Environments (`cogdiod.scm`)

Guile's first-class environments enable hypothetical reasoning without mutating the global AtomSpace:

```scheme
; Hypothetical: "What if the cat is sick?"
(define child-env (make-child-env global-env '((sick . (TV 0.95 0.90)))))
(eval '(deduce "sick->vet" "sick") child-env)
; => TV(0.855, 0.689)  — vs baseline TV(0.180, 0.306)

; Global AtomSpace is UNCHANGED
(lookup global-env "sick") ; => TV(0.200, 0.400)  ✓
```

---

## File Extension Convention

CogDiod uses explicit extensions to avoid the historic `.pl` Perl/Prolog collision:

| Language | Extension | Role |
| :--- | :--- | :--- |
| Elbo (Limbo/Lisp) | `.elm` | Dis VM bytecode packages (execution substrate) |
| Maude | `.maude` | Meta-level rule rewriting |
| **Perl** | **`.pl`** | Procedural glue, AUTOLOAD, system integration |
| **Prolog** | **`.pro`** | Declarative knowledge, Horn clause pattern matching |
| Guile Scheme | `.scm` | First-class environments, hypothetical reasoning |
| Racket | `.rkt` | `#lang` dialect tower, compile-time macros |
| Clojure | `.clj` | Persistent AtomSpace, protocols, STM |

---

## Building

Requires `zig cc` (or any C11 compiler) and `pthreads`:

```bash
cd cogdiod && make
./cogdiod_test   # 22 assertions, all PASS
```

For language integrations:

```bash
cd cogdiod-native && bash run_all.sh
```

Requires: `perl`, `clojure` (Java 11+), `hy` (`pip install hy`).

---

## Formal Specification

See [`CogDiod_Formal_Specification.md`](docs/CogDiod_Formal_Specification.md) for the complete type system, protocol definition, and lifecycle model.

---

## Related Work

- [OpenCog AtomSpace](https://github.com/opencog/atomspace)
- [Inferno OS / Dis VM](https://inferno-os.org/)
- [Limbo Programming Language](https://inferno-os.org/limbo/)
- [diod — 9P server](https://github.com/chaos/diod)
- [9P Protocol](https://9p.io/sys/man/5/INDEX.html)

---

## License

MIT

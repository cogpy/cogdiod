# CogDiod Quickstart: Racket Integration

## Prerequisites
- Racket 8.x: https://racket-lang.org/download/
- CogDiod bridge running: `./cogdiod_bridge &`

## Architecture Overview

The Racket integration demonstrates **language-oriented programming**
via `#lang` dialects. CogDiod provides:
- `#lang elbo/base` — standard Elbo module dialect
- `#lang elbo/temporal` — temporal reasoning DSL
- `#lang elbo/spatial` — spatial reasoning DSL

Each dialect's macro expander compiles down to bridge calls.

## Running the Demo

```bash
./cogdiod_bridge &
racket racket/cogdiod.rkt
```

## Hello Atom

```racket
#lang racket
(require "cogdiod.rkt")

(define bridge (cogdiod-connect))
(define cat-uuid (bridge-spawn! bridge "ConceptNode" "cat" 0.8 0.9))
(displayln (bridge-get-tv bridge cat-uuid))
;; => '((strength . 0.8) (confidence . 0.9))
```

## Temporal DSL

The `#lang elbo/temporal` dialect provides sugar for temporal relations:

```racket
#lang elbo/temporal
;; (before A B confidence) → ImplicationLink(A,B) TV=(c, 0.9*c)
(before "sunrise" "daylight" 0.99)

;; (after A B confidence) → ImplicationLink(B,A) TV=(c, 0.9*c)
(after "sunset" "darkness" 0.97)

;; (during A B confidence) → EvaluationLink(A,B) TV=(c, 0.9*c)
(during "rain" "wet-ground" 0.95)
```

This expands at read time (macro expansion) into:
```racket
(bridge-spawn! *bridge* "ImplicationLink" "sunrise->daylight" 0.99 0.891)
(bridge-link! *bridge* sunrise-uuid impl-uuid)
(bridge-link! *bridge* impl-uuid daylight-uuid)
```

## Spatial DSL

```racket
#lang elbo/spatial
;; (near A B confidence) → EvaluationLink(near, [A, B]) TV=(c, 0.9*c)
(near "cat" "mat" 0.9)
;; (contains A B confidence) → ImplicationLink(inside-A, B) TV=(c, 0.9*c)
(contains "bag" "book" 0.85)
```

## Key Insight

Racket's `#lang` allows cognitive sub-domains to have their own
syntax that compiles to standard bridge calls. A temporal reasoning
expert writes `(before sunrise daylight 0.99)` and gets a fully
configured CogDiod inference graph. No C required.

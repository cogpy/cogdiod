# CogDiod Quickstart: Guile Scheme Integration

## Prerequisites
- GNU Guile 3.x: `apt install guile-3.0` / `brew install guile`
- CogDiod bridge server running: `./cogdiod_bridge &`

## Architecture Overview

The Guile integration demonstrates **first-class environments** as
OpenCog Contexts (MicroTheories). Each environment is a Scheme scope
that can be:
- Created as a child of the global AtomSpace (non-destructively)
- Populated with hypothetical bindings
- Evaluated — running PLN inside that environment
- Serialized to JSON and sent over the bridge

## Running the Demo

```bash
./cogdiod_bridge &
guile guile/cogdiod.scm
```

## Hello Atom

```scheme
;; Connect to the bridge
(define bridge (cogdiod-connect))

;; Spawn atoms
(define cat-uuid    (bridge-spawn! bridge "ConceptNode" "cat" 0.8 0.9))
(define animal-uuid (bridge-spawn! bridge "ConceptNode" "animal" 0.5 0.6))

;; Read TV
(display (bridge-get-tv bridge cat-uuid))
;; => (strength: 0.8 confidence: 0.9)
```

## Hypothetical Reasoning

The Guile integration lets you ask "what if cat is sick?" without
mutating the global AtomSpace:

```scheme
;; Create a child environment forking the global context
(define hypothetical-env
  (make-child-env global-env))

;; In the child env, cat is sick (TV near 0.0)
(env-bind! hypothetical-env 'cat-tv '(0.05 0.9))

;; Run PLN inside the hypothetical — global untouched
(define result
  (with-env hypothetical-env
    (lambda ()
      (pln-deduce (env-lookup 'cat-tv)
                  (env-lookup 'cat->animal-tv)))))

;; The global cat TV is still 0.8
(display (bridge-get-tv bridge cat-uuid))
;; => (strength: 0.8 confidence: 0.9)
```

## Environment Serialization

Environments can be serialized to JSON for distributed reasoning:

```scheme
;; Serialize the hypothetical context
(define env-json (cogdiod-env->json hypothetical-env))
;; => {"bindings":{"cat-tv":[0.05,0.9],"cat->animal-tv":[0.95,0.8]}}

;; Send over the bridge for remote evaluation
(bridge-send-env bridge env-json remote-node-uuid)
```

## Key Insight

Scheme environments are first-class values. By capturing and
reifying them, CogDiod implements OpenCog's "MicroTheory" concept
natively — each hypothetical "what if?" context is just a Scheme
`let`-binding that can be passed, stored, and transmitted.

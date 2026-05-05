# CogDiod Quickstart: Clojure Integration

## Prerequisites
- Java 11+
- Clojure CLI (`brew install clojure/tools/clojure`)
- CogDiod bridge server built: `make bridge && ./cogdiod_bridge &`

## Architecture Overview

The Clojure integration connects to the bridge server over TCP on port 19999.
It provides:
- **Protocols** (`Cognitive`, `Visualizable`, `Learnable`) via `extend-protocol`
- **Persistent AtomSpace** (immutable versioned map using Clojure's HAM-Trie)
- **Episodic Memory** — query TV at any past version
- **STM** — `(dosync ...)` for concurrent MindAgent updates

## Running the Demo

```bash
# Terminal 1: start the bridge
make bridge
./cogdiod_bridge &

# Terminal 2: run the Clojure integration
clojure clojure/cogdiod.clj
```

## Hello Atom

```clojure
(bridge-connect!)

;; Spawn a ConceptNode with TV=(0.8, 0.9)
(def cat-uuid (bridge-spawn! "ConceptNode" "cat" 0.80 0.90))

;; Read it back
(println (bridge-get-tv cat-uuid))
;; => {:s 0.8, :c 0.9}

;; Create a local atom record
(def cat (make-atom cat-uuid "ConceptNode" "cat" 0.80 0.90))

;; Use the Cognitive protocol
(println (get-tv cat))
;; => {:s 0.8, :c 0.9}

;; Render via Visualizable (injected post-creation)
(println (render-text cat))
;; => ConceptNode('cat') [████████████░░░░░░░░] s=0.800 c=0.900
```

## Episodic Memory

```clojure
;; Take snapshots after each cognitive step
(def v0 @*atomspace*)
(transact-set-tv! cat-uuid 0.85 0.92)
(def v1 @*atomspace*)

;; Time-travel: query cat's TV at v0
(println (get-tv (space-get v0 cat-uuid)))
;; => {:s 0.8, :c 0.9}

;; And at v1
(println (get-tv (space-get v1 cat-uuid)))
;; => {:s 0.85, :c 0.92}
```

## Bridge Episodic Query

The bridge also stores the last 8 TV values per atom:
```bash
cogdiod-cli episodic <uuid> 0   # oldest saved TV
cogdiod-cli episodic <uuid> 1   # second-oldest
```

## Key Insight

`extend-protocol` adds `Visualizable` and `Learnable` to `AtomRecord`
**after** it was defined — the Expression Problem solved. No inheritance
required; any code in any namespace can extend any type.

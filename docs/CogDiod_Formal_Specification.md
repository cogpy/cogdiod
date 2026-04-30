# CogDiod: Formal Specification & Archetypal Implementation

**Author**: Manus AI
**Date**: April 27, 2026

This document provides the formal specification and data structures for an archetypal **CogDiod** entity. A CogDiod is a self-contained cognitive daemon that combines a 9P/DisTyx server, a Dis VM isolate pool, an Elbo (`.elm`) package loader, and a micro-kernel OpenCog runtime.

## 1. Core Data Structures

The core of CogDiod is written in C to ensure maximum performance and minimal overhead, managing the Dis VM isolates and the 9P server.

### 1.1 The Isolate Context

An `AtomIsolate` represents a single running Dis VM context (an Atom). It holds the VM state, a reference to its shared `.elm` bytecode, and its structural links (Limbo channels).

```c
typedef struct {
    uint64_t uuid;                 // Unique 9P Qid
    uint32_t type_id;              // Atom type (e.g., ConceptNode)
    
    // Dis VM State
    DisVMContext* vm_ctx;          // The VM registers, stack, and heap
    ElmPackage*   package;         // Shared pointer to loaded .elm bytecode
    
    // Cognitive State
    TruthValue    tv;              // Local TruthValue cache
    float         sti;             // Short-Term Importance (Attention)
    float         lti;             // Long-Term Importance
    
    // Topology (Limbo Channels)
    ChannelList*  incoming_links;  // Channels from Atoms pointing to this
    ChannelList*  outgoing_links;  // Channels to Atoms this points to
    
    // Concurrency
    pthread_mutex_t lock;
} AtomIsolate;
```

### 1.2 The Package Loader

The `ElmPackage` structure represents a loaded Elbo module. Bytecode is shared across all isolates of the same type.

```c
typedef struct {
    uint32_t type_id;              // Hash of the type name
    char     name[64];             // e.g., "ConceptNode"
    
    // Bytecode
    uint8_t* dis_bytecode;         // The compiled Limbo instructions
    size_t   bytecode_size;
    
    // Reference Counting
    uint32_t ref_count;            // Number of active isolates using this package
} ElmPackage;
```

### 1.3 The Micro-Kernel State

The `CogDiodKernel` manages the isolate pool, the package cache, and the 9P server state.

```c
typedef struct {
    // 9P Server State
    NpSrv* np_server;              // The diod 9P server instance
    
    // Isolate Management
    HashTable* atom_pool;          // UUID -> AtomIsolate*
    
    // Package Management
    HashTable* package_cache;      // type_id -> ElmPackage*
    
    // Scheduler
    ThreadPool* worker_pool;       // OS threads executing VM contexts
} CogDiodKernel;
```

## 2. The DisTyx Protocol

DisTyx extends the standard 9P2000.L protocol with cognitive operations. These operations are mapped to 9P messages.

| DisTyx Operation | 9P Message | Description |
| :--- | :--- | :--- |
| `SPAWN` | `Tlcreate` | Creates a new `AtomIsolate`. The filename specifies the type (e.g., `ConceptNode`). |
| `DESTROY` | `Tunlinkat` | Decrements the reference count. If zero, reclaims the isolate. |
| `READ_TV` | `Tread` | Reads the current TruthValue of the isolate. |
| `WRITE_TV` | `Twrite` | Updates the TruthValue. |
| `LINK` | `Tlink` | Establishes a Limbo channel between two isolates. |
| `ATTEND` | `Twrite` (xattr) | Sends STI currency to the isolate via its attention channel. |
| `INFER` | `Twrite` (xattr) | Triggers an inference cycle on a Link isolate. |

## 3. The Lifecycle of an Archetypal Entity

Let's trace the lifecycle of a `PredictiveImplicationLink` connecting two `ConceptNode` atoms (A and B).

### Phase 1: Package Resolution
1. A client sends a `Tlcreate` request to `CogDiod` to create a `PredictiveImplicationLink`.
2. `CogDiod` checks the `package_cache` for `PredictiveImplicationLink.elm`.
3. If missing, it fetches the `.elm` file from the network, compiles it (or loads pre-compiled `.dis`), and stores it in the `package_cache`.

### Phase 2: Isolate Allocation
1. `CogDiod` allocates a new `AtomIsolate`.
2. It assigns a new UUID (Qid).
3. It initializes a fresh `DisVMContext` and points its instruction pointer to the shared `ElmPackage` bytecode.

### Phase 3: Linking (Channel Establishment)
1. The client sends `Tlink` requests to connect the new Link to Node A and Node B.
2. `CogDiod` creates Limbo channels and adds them to the `outgoing_links` of the Link, and the `incoming_links` of the Nodes.

### Phase 4: Execution & Cognition
1. The isolate is added to the `worker_pool` run queue.
2. The VM executes the Elbo `(on-message ...)` loop.
3. If Node A's TruthValue changes, Node A sends a message down its outgoing channel.
4. The `PredictiveImplicationLink` receives the message, executes its inference logic (defined in its `.elm` package), and sends a message to Node B to update its TruthValue.

### Phase 5: Reclamation
1. The client sends a `Tunlinkat` request.
2. `CogDiod` closes the Limbo channels.
3. The `DisVMContext` is freed.
4. The `ElmPackage` reference count is decremented.

## 4. Elbo Package Archetypes

Below are the conceptual Elbo implementations for the core archetypes.

### 4.1 ConceptNode.elm

```lisp
(elbo-module ConceptNode
  (import AtomSpace "atomspace.m")
  (import ECAN "ecan.m")

  (state
    (tv  :SimpleTruthValue (strength 0.0) (confidence 0.0))
    (sti :float 0.0))

  ;; Handle external 9P Twrite to TruthValue
  (on-message :UpdateTV msg
    (set! tv (msg-tv msg))
    ;; Notify all outgoing links that our state changed
    (broadcast outgoing-channels (make-msg :SourceTVChanged tv)))

  ;; Handle ECAN attention allocation
  (on-message :Attend msg
    (set! sti (+ sti (msg-sti msg)))
    ;; If highly stimulated, spread attention to neighbors
    (if (> sti 1.0)
        (broadcast outgoing-channels (make-msg :Attend (/ sti 2.0))))))
```

### 4.2 PredictiveImplicationLink.elm

```lisp
(elbo-module PredictiveImplicationLink
  (import AtomSpace "atomspace.m")
  (import PLN "pln.m")

  (state
    (tv :SimpleTruthValue (strength 0.0) (confidence 0.0)))

  ;; React to source node changing
  (on-message :SourceTVChanged msg
    (let ((source-tv (msg-tv msg))
          (target-tv (request-tv target-channel)))
      
      ;; Perform PLN Deduction
      (let ((new-target-tv (PLN:deduce source-tv tv target-tv)))
        
        ;; Update target node
        (send target-channel (make-msg :UpdateTV new-target-tv))))))
```

## 5. Conclusion

This specification provides the blueprint for `CogDiod`. By treating Atoms as Dis VM isolates running Elbo packages, and routing their communication over Limbo channels managed by a 9P micro-kernel, we achieve a highly concurrent, modular, and scalable cognitive architecture.

# CogDiod Language Integrations: 5 Superpowers for Cognitive Architecture

This document summarises the five language-specific integrations implemented for the CogDiod architecture. Each language brings a unique programming paradigm that maps directly to a core requirement of a cognitive architecture like OpenCog.

The complete runnable implementation is located in `/home/ubuntu/cogdiod-lang/`.

---

## 1. Maude: Reflective Term Rewriting
**The Superpower:** Inference rules are first-class terms that can be rewritten by meta-rules.
**The Cognitive Map:** "Learning as differential term rewriting of inference rules."

In standard PLN, deduction rules are hardcoded. In the Maude integration, the deduction formula itself is stored in a `RuleRegistry` (the META-LEVEL). A meta-rule observes the performance of the deduction rule over time. If confidence is consistently low, the meta-rule **rewrites the deduction formula** into an abduction formula. The system learns *how* to reason by reasoning about its own rules.

## 2. Perl: Context-Sensitive Evaluation & AUTOLOAD
**The Superpower:** Expressions return different types based on context; undefined methods are dynamically dispatched.
**The Cognitive Map:** Atoms have multiple meanings; capabilities can be injected at runtime.

In the Perl integration, evaluating an Atom is context-sensitive:
- `LOGICAL` context returns its TruthValue.
- `ASSOCIATIVE` context returns a list of Hebbian-linked neighbours.
- `TEMPORAL` context returns an STI-discounted TruthValue.

Furthermore, `AUTOLOAD` allows us to inject new capabilities (like `ecan_spread` or `pln_deduce`) into running Atom isolates without modifying their underlying Elbo packages.

## 3. Racket: Language-Oriented Programming (`#lang`)
**The Superpower:** Every file defines its own language dialect; macros expand DSLs into base syntax.
**The Cognitive Map:** Cognitive sub-domains (temporal, spatial) get custom syntax that compiles to standard Dis VM bytecode.

The Racket integration implements a dialect tower. An Elbo package can declare `#lang elbo/temporal`. The developer writes `(before "event-a" "event-b" 0.5)`. The macro expander automatically transforms this into a complex subgraph of `ImplicationLink` and `EvaluationLink` atoms with time-decayed confidence, which is then compiled to base Dis VM bytecode.

## 4. Clojure: Protocols & Persistent Data Structures
**The Superpower:** Open dispatch via `defprotocol` and immutable version histories via HAM-Tries.
**The Cognitive Map:** The Expression Problem is solved for MindAgents; Episodic Memory is free.

The Clojure integration uses `extend-protocol` to add a `Visualizable` trait to `ConceptNode` long after the node type was defined in C. More importantly, it implements a `PersistentAtomSpace`. Every write returns a new version of the AtomSpace sharing structure with the previous version. This provides instant **Episodic Memory** — the system can query "What was the TV of 'cat' three versions ago?" without deep-copying the hypergraph.

## 5. Guile Scheme: First-Class Environments
**The Superpower:** Lexical scopes are first-class values that can be captured, chained, and evaluated within.
**The Cognitive Map:** Hypothetical reasoning and MicroTheory inheritance without mutating the global AtomSpace.

The Guile integration maps Scheme environments to OpenCog Contexts. To ask "What if the cat is sick?", the system creates a child environment, binds `sick` to a high TruthValue locally, and runs the PLN engine *inside that environment*. The global AtomSpace is never touched. Because environments are first-class, they can be reified to JSON and sent over the network (DisTyx) for distributed hypothetical reasoning.

---

## Conclusion
By mapping these advanced language features onto the Dis VM / 9P architecture, CogDiod achieves capabilities that would normally require massive amounts of custom C++ in a monolithic kernel. The combination of Maude's reflection, Perl's context, Racket's DSLs, Clojure's persistence, and Guile's environments provides a complete blueprint for a next-generation cognitive OS.

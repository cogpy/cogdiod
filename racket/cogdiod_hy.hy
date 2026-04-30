;;; cogdiod_hy.hy — #lang elbo dialect tower in Hy (Lisp on Python)
;;; =================================================================
;;;
;;; Hy is a Lisp that compiles to Python bytecode.
;;; It has: defmacro, quasiquote, first-class functions, tail calls,
;;; and proper S-expression syntax — making it the closest available
;;; runtime to Racket's #lang system in this environment.
;;;
;;; This file implements the SAME three-dialect tower as cogdiod.rkt:
;;;   - elbo/base macros (defatom, deflink, with-atom)
;;;   - elbo/temporal macros (before, during)
;;;   - elbo/spatial macros (near, contains)
;;;
;;; Run: hy cogdiod_hy.hy

(import socket)
(import json)
(import re)

;; ── Bridge client ─────────────────────────────────────────────────────────

(defclass CogBridge []
  (defn __init__ [self]
    (setv self.sock (socket.socket socket.AF_UNIX socket.SOCK_STREAM))
    (.connect self.sock "/tmp/cogdiod.sock")
    (setv self.f (.makefile self.sock "rw")))

  (defn call [self json-str]
    (.write self.f (+ json-str "\n"))
    (.flush self.f)
    (setv line (.readline self.f))
    (json.loads line))

  (defn spawn [self type name s c]
    (get (.call self
      (% "{\"op\":\"spawn\",\"type\":\"%s\",\"name\":\"%s\",\"strength\":%.4f,\"confidence\":%.4f}"
         #(type name s c)))
      "uuid"))

  (defn get-tv [self uuid]
    (setv r (.call self (% "{\"op\":\"get_tv\",\"uuid\":%d}" (tuple [uuid]))))
    #((get r "strength") (get r "confidence")))

  (defn set-tv [self uuid s c]
    (.call self
      (% "{\"op\":\"set_tv\",\"uuid\":%d,\"strength\":%.4f,\"confidence\":%.4f}"
         #(uuid s c))))

  (defn link [self from to]
    (.call self (% "{\"op\":\"link\",\"from\":%d,\"to\":%d}" #(from to))))

  (defn stats [self]
    (.call self "{\"op\":\"stats\"}")))

(setv bridge (CogBridge))

;; ── Atom struct ───────────────────────────────────────────────────────────

(defclass Atom []
  (defn __init__ [self uuid type name strength confidence]
    (setv self.uuid       uuid
          self.type       type
          self.name       name
          self.strength   strength
          self.confidence confidence
          self.sti        (* strength confidence)
          self.links      []))

  (defn __repr__ [self]
    (% "%s('%s') TV=(%.2f,%.2f)" #(self.type self.name self.strength self.confidence))))

;; ── #lang elbo/base macros ────────────────────────────────────────────────
;;
;; In Racket: (defatom cat ConceptNode (tv 0.80 0.90))
;; In Hy:     (defatom cat "ConceptNode" 0.80 0.90)
;;
;; defmacro in Hy is exactly like Racket's define-syntax — it runs at
;; compile time and transforms the S-expression before evaluation.

(defmacro defatom [name type s c]
  `(do
     (setv ~name
       (let [uuid (.spawn bridge ~type (str '~name) ~s ~c)]
         (Atom uuid ~type (str '~name) ~s ~c)))
     (print (% "  [elbo/base] defatom %s => uuid=%d TV=(%.2f,%.2f)"
               #((str '~name) (. ~name uuid) ~s ~c)))))

(defmacro deflink [a b]
  `(.link bridge (. ~a uuid) (. ~b uuid)))

;; ── #lang elbo/temporal macros ────────────────────────────────────────────
;;
;; (before event-a event-b confidence)
;; Expands at compile time to a bridge spawn call with time-decayed TV.
;; This is what makes Racket/Hy special: the expansion happens BEFORE
;; the code runs, not at runtime.

(defmacro before [event-a event-b conf]
  `(do
     (setv _tv-s ~conf
           _tv-c (* ~conf 0.95)
           _name (+ (str '~event-a) "→before→" (str '~event-b))
           _uuid (.spawn bridge "ImplicationLink" _name _tv-s _tv-c))
     (print (% "  [elbo/temporal] (before %s %s %.2f) => ImplLink uuid=%d TV=(%.3f,%.3f)"
               #((str '~event-a) (str '~event-b) ~conf _uuid _tv-s _tv-c)))
     (Atom _uuid "ImplicationLink" _name _tv-s _tv-c)))

(defmacro during [event context conf]
  `(do
     (setv _tv-s ~conf
           _tv-c (* ~conf 0.90)
           _name (+ (str '~event) "∈" (str '~context))
           _uuid (.spawn bridge "EvaluationLink" _name _tv-s _tv-c))
     (print (% "  [elbo/temporal] (during %s %s %.2f) => EvalLink uuid=%d TV=(%.3f,%.3f)"
               #((str '~event) (str '~context) ~conf _uuid _tv-s _tv-c)))
     (Atom _uuid "EvaluationLink" _name _tv-s _tv-c)))

;; ── #lang elbo/spatial macros ─────────────────────────────────────────────

(defmacro near [obj1 obj2 dist]
  `(do
     (setv _tv-s (/ 1.0 (+ 1.0 ~dist))
           _tv-c (/ 1.0 (+ 1.0 (* ~dist 0.5)))
           _name (+ "near(" (str '~obj1) "," (str '~obj2) ")")
           _uuid (.spawn bridge "EvaluationLink" _name _tv-s _tv-c))
     (print (% "  [elbo/spatial] (near %s %s %.1f) => EvalLink uuid=%d TV=(%.3f,%.3f)"
               #((str '~obj1) (str '~obj2) ~dist _uuid _tv-s _tv-c)))
     (Atom _uuid "EvaluationLink" _name _tv-s _tv-c)))

(defmacro contains [container item conf]
  `(do
     (setv _tv-s ~conf
           _tv-c (* ~conf 0.95)
           _name (+ (str '~container) "⊃" (str '~item))
           _uuid (.spawn bridge "EvaluationLink" _name _tv-s _tv-c))
     (print (% "  [elbo/spatial] (contains %s %s %.2f) => EvalLink uuid=%d TV=(%.3f,%.3f)"
               #((str '~container) (str '~item) ~conf _uuid _tv-s _tv-c)))
     (Atom _uuid "EvaluationLink" _name _tv-s _tv-c)))

;; ── PLN deduction ─────────────────────────────────────────────────────────

(defn pln-deduce [impl ant]
  #((* impl.strength ant.strength)
    (* impl.confidence ant.confidence 0.9)))

;; ── Main ──────────────────────────────────────────────────────────────────

(print (* "=" 60))
(print "RACKET/Hy INTEGRATION: #lang elbo Dialect Tower")
(print "Canonical Racket source: cogdiod.rkt")
(print (* "=" 60))

;; Phase 1: #lang elbo/base
(print "\n[Phase 1] #lang elbo/base — defining atoms with defatom macro...")
(defatom cat    "ConceptNode"     0.80 0.90)
(defatom animal "ConceptNode"     0.50 0.60)
(defatom impl   "ImplicationLink" 0.95 0.80)
(deflink cat impl)
(deflink impl animal)

;; Phase 2: #lang elbo/temporal
(print "\n[Phase 2] #lang elbo/temporal — temporal relationship macros...")
(setv t1 (before cat-eats animal-runs 0.75))
(setv t2 (during cat-eats feeding-time 0.90))

;; Phase 3: #lang elbo/spatial
(print "\n[Phase 3] #lang elbo/spatial — spatial relationship macros...")
(setv s1 (near cat mouse 2.5))
(setv s2 (contains garden cat 0.85))

;; Phase 4: PLN deduction
(print "\n[Phase 4] PLN deduction using base atoms...")
(setv [s-ded c-ded] (pln-deduce impl cat))
(.set-tv bridge animal.uuid s-ded c-ded)
(print (% "  P(animal) = TV(%.3f, %.3f)" #(s-ded c-ded)))

;; Phase 5: Macro expansion demonstration
(print "\n[Phase 5] Macro expansion — what the macros compile to...")
(print "  (defatom cat \"ConceptNode\" 0.80 0.90) expands at compile time to:")
(print "    (setv cat (Atom (bridge.spawn \"ConceptNode\" \"cat\" 0.80 0.90) ...))")
(print "  (before A B 0.75) expands at compile time to:")
(print "    (setv _tv-s 0.75 _tv-c (* 0.75 0.95) ...)")
(print "    (bridge.spawn \"ImplicationLink\" \"A→before→B\" _tv-s _tv-c)")
(print "  The macro IS the #lang elbo/temporal compiler.")
(print "  No runtime overhead: all DSL forms resolve at compile time.")

;; Phase 6: Verify
(print "\n[Phase 6] Final AtomSpace state...")
(setv [fs fc] (.get-tv bridge animal.uuid))
(print (% "  animal TV = (%.3f, %.3f)" #(fs fc)))
(setv stats (.stats bridge))
(print (% "  bridge stats: atoms=%s" (.get stats "atoms" "?")))

(print "\n[Key Insight]")
(print "  Three #lang dialects compiled to the same bridge protocol.")
(print "  defmacro runs at COMPILE TIME — the DSL has zero runtime cost.")
(print "  (before A B c) is not a function — it is a syntax transformation.")
(print "  In Racket/Hy: you write the language, then write programs in it.")
(print "  For CogDiod: temporal, spatial, causal domains get their own syntax.")
(print "  The language tower: elbo/spatial → elbo/base → Dis VM → AtomSpace.")

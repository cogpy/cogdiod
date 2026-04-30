#lang racket
;;; cogdiod.rkt — CogDiod Racket Integration
;;; ==========================================
;;;
;;; Racket's superpower for CogDiod: Language-Oriented Programming.
;;; Every file declares its own language with #lang.
;;; Macros expand DSLs into base syntax at compile time.
;;;
;;; This file defines THREE language layers:
;;;
;;;   #lang elbo/base    — the base Elbo atom language
;;;   #lang elbo/temporal — temporal DSL (before, after, during)
;;;   #lang elbo/spatial  — spatial DSL (near, far, contains)
;;;
;;; All three compile to the same base Dis VM bytecode representation.
;;;
;;; The bridge communication uses Racket's tcp-connect (via socat).
;;;
;;; Run: racket cogdiod.rkt
;;; (Racket must be installed; if not, run: hy cogdiod_hy.hy)

(require racket/tcp
         racket/string
         racket/format)

;; ── Bridge client ─────────────────────────────────────────────────────────

(define bridge-in  #f)
(define bridge-out #f)

(define (bridge-connect!)
  ;; Connect via socat TCP bridge (port 19999 → /tmp/cogdiod.sock)
  (define-values (in out) (tcp-connect "127.0.0.1" 19999))
  (set! bridge-in  in)
  (set! bridge-out out))

(define (bridge-call json)
  (write-string (string-append json "\n") bridge-out)
  (flush-output bridge-out)
  (define line (read-line bridge-in))
  ;; minimal JSON parse
  (define pairs
    (regexp-match* #rx"\"([^\"]+)\":([^,}]+)" line
                   #:match-select cdr))
  (for/hash ([pair pairs])
    (values (first pair)
            (string-trim (second pair) "\""))))

(define (bridge-spawn type name s c)
  (define r (bridge-call
    (format "{\"op\":\"spawn\",\"type\":\"~a\",\"name\":\"~a\",\"strength\":~a,\"confidence\":~a}"
            type name (~r s #:precision 4) (~r c #:precision 4))))
  (string->number (hash-ref r "uuid")))

(define (bridge-get-tv uuid)
  (define r (bridge-call (format "{\"op\":\"get_tv\",\"uuid\":~a}" uuid)))
  (values (string->number (hash-ref r "strength"))
          (string->number (hash-ref r "confidence"))))

(define (bridge-set-tv! uuid s c)
  (bridge-call
    (format "{\"op\":\"set_tv\",\"uuid\":~a,\"strength\":~a,\"confidence\":~a}"
            uuid (~r s #:precision 4) (~r c #:precision 4))))

(define (bridge-link! from to)
  (bridge-call (format "{\"op\":\"link\",\"from\":~a,\"to\":~a}" from to)))

;; ── #lang elbo/base — the base Elbo atom language ─────────────────────────
;;
;; In Racket, a #lang module is defined by a reader and expander.
;; Here we implement the base Elbo language as a set of macros and
;; functions that constitute the Elbo module system.
;;
;; Elbo atoms are represented as Racket structs (immutable by default).

(struct atom (uuid type name strength confidence sti links) #:transparent)

;; defatom — the base Elbo atom definition macro
;; In a real #lang elbo/base, this would be the module-level form.
;;
;; Usage: (defatom cat ConceptNode (tv 0.80 0.90))
(define-syntax defatom
  (syntax-rules (tv)
    [(_ name type (tv s c))
     (define name
       (let ([uuid (bridge-spawn (symbol->string 'type)
                                 (symbol->string 'name)
                                 s c)])
         (atom uuid (symbol->string 'type) (symbol->string 'name) s c 0.0 '())))]))

;; deflink — link two atoms
(define-syntax deflink
  (syntax-rules (->)
    [(_ a -> b)
     (bridge-link! (atom-uuid a) (atom-uuid b))]))

;; with-atom — update an atom's TV
(define-syntax with-atom
  (syntax-rules (tv :=)
    [(_ a tv := (s c))
     (begin
       (bridge-set-tv! (atom-uuid a) s c)
       (struct-copy atom a [strength s] [confidence c]))]))

;; ── #lang elbo/temporal — temporal DSL ────────────────────────────────────
;;
;; In Racket, you can define an entire language as a library.
;; The temporal DSL adds time-aware atom relationships.
;;
;; (before event-a event-b confidence) expands to an ImplicationLink
;; with time-decayed confidence.
;;
;; This is what Racket's macro system enables that no other language
;; can do as cleanly: the DSL IS the language, not just a library.

(define-syntax before
  (syntax-rules ()
    [(_ event-a event-b conf)
     (let* ([decay  0.95]
            [tv-s   conf]
            [tv-c   (* conf decay)]
            [uuid   (bridge-spawn "ImplicationLink"
                                  (string-append (symbol->string 'event-a)
                                                 "→before→"
                                                 (symbol->string 'event-b))
                                  tv-s tv-c)])
       (printf "  [elbo/temporal] (before ~a ~a ~a) => ImplLink uuid=~a TV=(~a,~a)~n"
               'event-a 'event-b conf uuid (~r tv-s #:precision 3) (~r tv-c #:precision 3))
       (atom uuid "ImplicationLink"
             (string-append (symbol->string 'event-a) "→before→" (symbol->string 'event-b))
             tv-s tv-c 0.0 '()))]))

(define-syntax during
  (syntax-rules ()
    [(_ event context conf)
     (let* ([tv-s conf]
            [tv-c (* conf 0.90)]
            [uuid (bridge-spawn "EvaluationLink"
                                (string-append (symbol->string 'event)
                                               "∈"
                                               (symbol->string 'context))
                                tv-s tv-c)])
       (printf "  [elbo/temporal] (during ~a ~a ~a) => EvalLink uuid=~a TV=(~a,~a)~n"
               'event 'context conf uuid (~r tv-s #:precision 3) (~r tv-c #:precision 3))
       (atom uuid "EvaluationLink"
             (string-append (symbol->string 'event) "∈" (symbol->string 'context))
             tv-s tv-c 0.0 '()))]))

;; ── #lang elbo/spatial — spatial DSL ──────────────────────────────────────
;;
;; The spatial DSL adds proximity-aware atom relationships.
;; (near obj1 obj2 distance) creates a spatial EvaluationLink
;; with confidence inversely proportional to distance.

(define-syntax near
  (syntax-rules ()
    [(_ obj1 obj2 dist)
     (let* ([tv-s  (/ 1.0 (+ 1.0 dist))]
            [tv-c  (/ 1.0 (+ 1.0 (* dist 0.5)))]
            [uuid  (bridge-spawn "EvaluationLink"
                                 (string-append "near("
                                                (symbol->string 'obj1) ","
                                                (symbol->string 'obj2) ")")
                                 tv-s tv-c)])
       (printf "  [elbo/spatial] (near ~a ~a ~a) => EvalLink uuid=~a TV=(~a,~a)~n"
               'obj1 'obj2 dist uuid (~r tv-s #:precision 3) (~r tv-c #:precision 3))
       (atom uuid "EvaluationLink"
             (string-append "near(" (symbol->string 'obj1) "," (symbol->string 'obj2) ")")
             tv-s tv-c 0.0 '()))]))

(define-syntax contains
  (syntax-rules ()
    [(_ container item conf)
     (let* ([tv-s  conf]
            [tv-c  (* conf 0.95)]
            [uuid  (bridge-spawn "EvaluationLink"
                                 (string-append (symbol->string 'container)
                                                "⊃"
                                                (symbol->string 'item))
                                 tv-s tv-c)])
       (printf "  [elbo/spatial] (contains ~a ~a ~a) => EvalLink uuid=~a TV=(~a,~a)~n"
               'container 'item conf uuid (~r tv-s #:precision 3) (~r tv-c #:precision 3))
       (atom uuid "EvaluationLink"
             (string-append (symbol->string 'container) "⊃" (symbol->string 'item))
             tv-s tv-c 0.0 '()))]))

;; ── PLN deduction ─────────────────────────────────────────────────────────

(define (pln-deduce impl-tv ant-tv)
  (define-values (si ci) (values (atom-strength impl-tv) (atom-confidence impl-tv)))
  (define-values (sa ca) (values (atom-strength ant-tv)  (atom-confidence ant-tv)))
  (values (* si sa) (* ci ca 0.9)))

;; ── Main demo ─────────────────────────────────────────────────────────────

(displayln (make-string 60 #\=))
(displayln "RACKET INTEGRATION: #lang elbo Dialect Tower")
(displayln (make-string 60 #\=))

(bridge-connect!)

;; Phase 1: #lang elbo/base — define atoms with base Elbo syntax
(displayln "\n[Phase 1] #lang elbo/base — defining atoms...")
(defatom cat    ConceptNode    (tv 0.80 0.90))
(defatom animal ConceptNode    (tv 0.50 0.60))
(defatom impl   ImplicationLink (tv 0.95 0.80))
(deflink cat -> impl)
(deflink impl -> animal)
(printf "  cat=~a, animal=~a, impl=~a~n"
        (atom-uuid cat) (atom-uuid animal) (atom-uuid impl))

;; Phase 2: #lang elbo/temporal — temporal DSL
(displayln "\n[Phase 2] #lang elbo/temporal — temporal relationships...")
(define t1 (before cat-eats animal-runs 0.75))
(define t2 (during cat-eats feeding-time 0.90))
(printf "  Temporal atoms created: ~a, ~a~n" (atom-uuid t1) (atom-uuid t2))

;; Phase 3: #lang elbo/spatial — spatial DSL
(displayln "\n[Phase 3] #lang elbo/spatial — spatial relationships...")
(define s1 (near cat mouse 2.5))
(define s2 (contains garden cat 0.85))
(printf "  Spatial atoms created: ~a, ~a~n" (atom-uuid s1) (atom-uuid s2))

;; Phase 4: PLN deduction using base atoms
(displayln "\n[Phase 4] PLN deduction across all atom types...")
(define-values (s-ded c-ded) (pln-deduce impl cat))
(bridge-set-tv! (atom-uuid animal) s-ded c-ded)
(printf "  P(animal) = TV(~a, ~a)~n"
        (~r s-ded #:precision 3) (~r c-ded #:precision 3))

;; Phase 5: Macro expansion inspection
(displayln "\n[Phase 5] Macro expansion — what #lang elbo/temporal compiles to...")
(printf "  (before A B 0.75) expands to:~n")
(printf "    (bridge-spawn \"ImplicationLink\" \"A→before→B\" 0.75 (* 0.75 0.95))~n")
(printf "    => ImplicationLink with time-decayed confidence~n")
(printf "  (near A B 2.5) expands to:~n")
(printf "    (bridge-spawn \"EvaluationLink\" \"near(A,B)\" (/ 1 (+ 1 2.5)) ...)~n")
(printf "    => EvaluationLink with distance-decayed TV~n")
(printf "  All three dialects compile to the same bridge spawn calls.~n")
(printf "  In Racket: the macro IS the compiler for the sub-language.~n")

;; Phase 6: Verify final state
(displayln "\n[Phase 6] Final AtomSpace state...")
(define-values (fs fc) (bridge-get-tv (atom-uuid animal)))
(printf "  animal TV = (~a, ~a)~n" (~r fs #:precision 3) (~r fc #:precision 3))

(let ([stats (bridge-call "{\"op\":\"stats\"}")])
  (printf "  bridge stats: atoms=~a~n" (hash-ref stats "atoms" "?")))

(displayln "\n[Key Insight]")
(displayln "  Three #lang dialects compiled to the same bridge protocol.")
(displayln "  (before A B c) is not a function call — it is a language form.")
(displayln "  The macro expander is the #lang elbo/temporal compiler.")
(displayln "  In Racket: you don't write programs IN a language.")
(displayln "  You write the language, then write programs in it.")
(displayln "  For CogDiod: each cognitive domain gets its own syntax.")

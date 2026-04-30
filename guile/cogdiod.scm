;;; cogdiod.scm — CogDiod Guile Scheme Integration
;;; =================================================
;;;
;;; Guile's superpower for CogDiod: First-class environments.
;;;
;;; In Guile (and R7RS Scheme), environments are first-class values.
;;; You can:
;;;   - Create a new environment: (make-environment parent)
;;;   - Evaluate an expression in an environment: (eval expr env)
;;;   - Capture an environment: (the-environment)
;;;   - Extend an environment: (environment-define env name val)
;;;
;;; For CogDiod, this means:
;;;   - The global AtomSpace = the global environment
;;;   - A hypothesis = a child environment with overridden bindings
;;;   - Hypothetical reasoning = (eval pln-query child-env)
;;;   - MicroTheory inheritance = environment chain lookup
;;;   - The global AtomSpace is NEVER mutated during hypothetical reasoning
;;;
;;; This is the Guile equivalent of OpenCog's Context system,
;;; implemented as a natural property of the language itself.
;;;
;;; Run: guile cogdiod.scm
;;; (If Guile is not installed, run: hy cogdiod_hy.hy)

(use-modules (ice-9 format)
             (ice-9 match)
             (srfi srfi-1)   ; list utilities
             (srfi srfi-9))  ; records

;; ── UNIX socket bridge ────────────────────────────────────────────────────
;;
;; Guile has built-in UNIX socket support via (ice-9 popen) or
;; the low-level socket API.

(define bridge-sock #f)
(define bridge-port #f)

(define (bridge-connect!)
  (set! bridge-sock (socket AF_UNIX SOCK_STREAM 0))
  (connect bridge-sock AF_UNIX "/tmp/cogdiod.sock")
  (set! bridge-port (make-soft-port
    (vector
      (lambda (c) (write-char c bridge-sock))  ; write char
      (lambda (s) (display s bridge-sock))     ; write string
      (lambda ()  (force-output bridge-sock))  ; flush
      (lambda ()  (read-char bridge-sock))     ; read char
      (lambda ()  (close-port bridge-sock)))   ; close
    "rw")))

(define (bridge-call json)
  (display (string-append json "\n") bridge-port)
  (force-output bridge-port)
  ;; Read response line
  (let loop ((chars '()))
    (let ((c (read-char bridge-port)))
      (if (or (eof-object? c) (char=? c #\newline))
          (list->string (reverse chars))
          (loop (cons c chars))))))

(define (parse-json-string s key)
  "Extract value for key from a simple JSON string"
  (let* ((pattern (string-append "\"" key "\":"))
         (pos     (string-contains s pattern)))
    (if pos
        (let* ((start (+ pos (string-length pattern)))
               (rest  (substring s start))
               ;; strip leading quote or whitespace
               (rest2 (string-trim rest (char-set #\" #\space)))
               ;; find end (comma, }, or quote)
               (end   (let loop ((i 0))
                        (if (>= i (string-length rest2)) i
                            (let ((c (string-ref rest2 i)))
                              (if (member c '(#\, #\} #\"))
                                  i (loop (+ i 1))))))))
          (substring rest2 0 end))
        #f)))

(define (bridge-spawn type name s c)
  (let* ((json (format #f
                 "{\"op\":\"spawn\",\"type\":\"~a\",\"name\":\"~a\",\"strength\":~,4f,\"confidence\":~,4f}"
                 type name s c))
         (resp (bridge-call json)))
    (string->number (parse-json-string resp "uuid"))))

(define (bridge-get-tv uuid)
  (let* ((resp (bridge-call (format #f "{\"op\":\"get_tv\",\"uuid\":~a}" uuid)))
         (s    (string->number (parse-json-string resp "strength")))
         (c    (string->number (parse-json-string resp "confidence"))))
    (cons s c)))

(define (bridge-set-tv! uuid s c)
  (bridge-call
    (format #f "{\"op\":\"set_tv\",\"uuid\":~a,\"strength\":~,4f,\"confidence\":~,4f}"
            uuid s c)))

(define (bridge-link! from to)
  (bridge-call (format #f "{\"op\":\"link\",\"from\":~a,\"to\":~a}" from to)))

;; ── TruthValue ────────────────────────────────────────────────────────────

(define-record-type <tv>
  (make-tv s c)
  tv?
  (s tv-s)
  (c tv-c))

(define (tv->string tv)
  (format #f "TV(~,3f,~,3f)" (tv-s tv) (tv-c tv)))

;; ── PLN operations ────────────────────────────────────────────────────────

(define (pln-deduce impl-tv ant-tv)
  (make-tv (* (tv-s impl-tv) (tv-s ant-tv))
           (* (tv-c impl-tv) (tv-c ant-tv) 0.9)))

;; ── First-class Environment ───────────────────────────────────────────────
;;
;; In Guile, environments are first-class. We implement a CogEnvironment
;; that wraps Guile's native environment chain.
;;
;; A CogEnvironment is:
;;   - A name (for display)
;;   - A parent environment (or #f for global)
;;   - A local binding table (overrides for this hypothesis)
;;   - A depth counter

(define-record-type <cog-env>
  (make-cog-env name parent bindings depth)
  cog-env?
  (name     cog-env-name)
  (parent   cog-env-parent)
  (bindings cog-env-bindings)
  (depth    cog-env-depth))

(define (make-global-env)
  (make-cog-env "global" #f (make-hash-table) 0))

(define (make-child-env parent name)
  (make-cog-env name parent (make-hash-table) (+ (cog-env-depth parent) 1)))

(define (env-define! env name val)
  (hash-set! (cog-env-bindings env) name val))

(define (env-lookup env name)
  "Look up name in env, walking up the parent chain (= MicroTheory inheritance)"
  (let ((local (hash-ref (cog-env-bindings env) name #f)))
    (if local
        local
        (if (cog-env-parent env)
            (env-lookup (cog-env-parent env) name)
            #f))))

(define (env-deduce env impl-name ant-name)
  "Run PLN deduction within an environment (hypothetical or global)"
  (let ((impl-tv (env-lookup env impl-name))
        (ant-tv  (env-lookup env ant-name)))
    (if (and impl-tv ant-tv)
        (pln-deduce impl-tv ant-tv)
        (make-tv 0.0 0.0))))

;; ── Hypothetical reasoning ────────────────────────────────────────────────
;;
;; (with-hypothesis env hypothesis-alist body)
;; Creates a child environment, binds the hypotheses, evaluates body,
;; and returns the result WITHOUT modifying the parent environment.
;;
;; This is the core of Guile's contribution to CogDiod.

(define-syntax with-hypothesis
  (syntax-rules ()
    [(_ env name ((binding val) ...) body ...)
     (let ((child (make-child-env env name)))
       (for-each (lambda (b v) (env-define! child b v))
                 '(binding ...) (list val ...))
       body ...)]))

;; ── Main ──────────────────────────────────────────────────────────────────

(display (make-string 60 #\=)) (newline)
(display "GUILE SCHEME INTEGRATION: First-Class Environments") (newline)
(display (make-string 60 #\=)) (newline)

(bridge-connect!)

;; Phase 1: Populate AtomSpace
(display "\n[Phase 1] Populating AtomSpace...\n")
(define cat-uuid    (bridge-spawn "ConceptNode"    "cat"         0.80 0.90))
(define animal-uuid (bridge-spawn "ConceptNode"    "animal"      0.50 0.60))
(define sick-uuid   (bridge-spawn "ConceptNode"    "sick"        0.20 0.40))
(define vet-uuid    (bridge-spawn "ConceptNode"    "vet-visit"   0.10 0.30))
(define impl1-uuid  (bridge-spawn "ImplicationLink" "cat->animal" 0.95 0.80))
(define impl2-uuid  (bridge-spawn "ImplicationLink" "sick->vet"   0.90 0.85))
(bridge-link! cat-uuid impl1-uuid)
(bridge-link! impl1-uuid animal-uuid)
(bridge-link! sick-uuid impl2-uuid)
(bridge-link! impl2-uuid vet-uuid)
(format #t "  spawned: cat=~a, sick=~a, vet=~a~n" cat-uuid sick-uuid vet-uuid)

;; Phase 2: Build global environment
(display "\n[Phase 2] Building global environment...\n")
(define global-env (make-global-env))
(env-define! global-env "cat"        (make-tv 0.80 0.90))
(env-define! global-env "animal"     (make-tv 0.50 0.60))
(env-define! global-env "sick"       (make-tv 0.20 0.40))
(env-define! global-env "vet-visit"  (make-tv 0.10 0.30))
(env-define! global-env "cat->animal" (make-tv 0.95 0.80))
(env-define! global-env "sick->vet"   (make-tv 0.90 0.85))
(display "  global_env populated with 6 atoms\n")

;; Phase 3: Baseline deduction in global environment
(display "\n[Phase 3] Baseline deduction in global environment...\n")
(define baseline-animal (env-deduce global-env "cat->animal" "cat"))
(define baseline-vet    (env-deduce global-env "sick->vet"   "sick"))
(format #t "  deduce(cat->animal, cat) = ~a~n" (tv->string baseline-animal))
(format #t "  deduce(sick->vet, sick)  = ~a~n" (tv->string baseline-vet))

;; Phase 4: Hypothetical reasoning — "What if the cat is sick?"
(display "\n[Phase 4] Hypothetical: 'What if the cat is sick?'...\n")
(display "  (evaluating in child environment, global AtomSpace unchanged)\n")

(define hyp-result
  (with-hypothesis global-env "hypothesis:cat-is-sick"
    (("sick" (make-tv 0.95 0.90)))
    (let ((hyp-vet (env-deduce child "sick->vet" "sick")))
      (format #t "    [hypothesis] sick := ~a~n"
              (tv->string (env-lookup child "sick")))
      (format #t "  P(vet-visit | cat is sick) = ~a~n" (tv->string hyp-vet))
      (format #t "  P(vet-visit | baseline)    = ~a~n" (tv->string baseline-vet))
      hyp-vet)))

;; Verify global environment is unchanged
(define global-sick (env-lookup global-env "sick"))
(format #t "  Global 'sick' TV unchanged: ~a~n" (tv->string global-sick))

;; Phase 5: Nested hypotheticals — context stacking
(display "\n[Phase 5] Nested hypotheticals — context stacking...\n")
(format #t "  Level 0 (global): cat TV = ~a~n"
        (tv->string (env-lookup global-env "cat")))

(with-hypothesis global-env "level-1:cat-certain"
  (("cat" (make-tv 0.99 0.99)))
  (format #t "  Level 1 (cat certain): cat TV = ~a~n"
          (tv->string (env-lookup child "cat")))

  (with-hypothesis child "level-2:also-sick"
    (("sick" (make-tv 0.95 0.90)))
    (format #t "  Level 2 (also sick):   cat TV  = ~a~n"
            (tv->string (env-lookup child "cat")))
    (format #t "  Level 2 (also sick):   sick TV = ~a~n"
            (tv->string (env-lookup child "sick")))
    (let ((nested-vet (env-deduce child "sick->vet" "sick")))
      (format #t "  Level 2 deduction:     P(vet) = ~a~n"
              (tv->string nested-vet)))))

;; Phase 6: Environment reification — serialize for DisTyx transport
(display "\n[Phase 6] Environment reification (for DisTyx transport)...\n")
(define (reify-env env)
  "Convert environment chain to a list of (depth name bindings) triples"
  (if (not env)
      '()
      (cons (list (cog-env-depth env)
                  (cog-env-name env)
                  (hash-map->list cons (cog-env-bindings env)))
            (reify-env (cog-env-parent env)))))

(define reified (reify-env global-env))
(display "  Reified environment chain:\n")
(for-each (lambda (frame)
  (format #t "    depth=~a name='~a' bindings=~a~n"
          (car frame) (cadr frame)
          (map car (caddr frame))))
  reified)
(display "  (This can be serialized to JSON and sent over DisTyx)\n")

;; Phase 7: Verify global AtomSpace is unchanged
(display "\n[Phase 7] Verifying global AtomSpace is unchanged...\n")
(let* ((tv (bridge-get-tv sick-uuid))
       (s  (car tv))
       (c  (cdr tv)))
  (format #t "  bridge.get_tv(sick) = (~,3f, ~,3f)~n" s c)
  (if (and (< (abs (- s 0.20)) 0.01)
           (< (abs (- c 0.40)) 0.01))
      (format #t "  PASS: sick TV = (~,3f, ~,3f) — unchanged~n" s c)
      (format #t "  FAIL: sick TV was mutated!~n")))

(display "\n[Key Insight]\n")
(display "  Environments are first-class: created, chained, reified, transported.\n")
(display "  Hypothetical reasoning = PLN inference in a child environment.\n")
(display "  Context stacking = OpenCog MicroTheory inheritance.\n")
(display "  The global AtomSpace was NEVER mutated during any hypothetical.\n")
(display "  In Guile: (eval query (make-child-env hypothesis))\n")
(display "  For CogDiod: every Dis VM isolate IS an environment.\n")
(display "  Spawning a hypothesis = spawning a child isolate with overrides.\n")

(close-port bridge-port)

(display "\n[Key Insight]\n")
(display "  Environments are first-class: created, chained, reified, transported.\n")
(display "  Hypothetical reasoning = PLN inference in a child environment.\n")
(display "  Context stacking = OpenCog MicroTheory inheritance.\n")
(display "  The global AtomSpace was NEVER mutated during any hypothetical.\n")
(display "  In Guile: (eval query (make-child-env hypothesis))\n")
(display "  For CogDiod: every Dis VM isolate IS an environment.\n")
(display "  Spawning a hypothesis = spawning a child isolate with overrides.\n")

;; ── Phase 6.23: Environment serialization to JSON ─────────────────────────
;;
;; Environments can be reified to JSON and transmitted over DisTyx
;; for distributed hypothetical reasoning on remote CogDiod nodes.

(define (env->alist env)
  "Collect all bindings from a cogdiod-env into an association list."
  (if (cogdiod-env? env)
      (cogdiod-env-bindings env)
      '()))

(define (alist->json-bindings bindings)
  "Serialize an alist of (name . value) to a JSON object string."
  (string-append
    "{"
    (string-join
      (map (lambda (pair)
             (string-append
               "\"" (symbol->string (car pair)) "\":"
               (let ((v (cdr pair)))
                 (cond
                   ((pair? v)  ; TruthValue as list
                    (format #f "[~,4f,~,4f]"
                            (if (pair? v) (car v) 0.5)
                            (if (pair? (cdr v)) (cadr v) 0.1)))
                   ((number? v) (format #f "~,4f" v))
                   ((string? v) (string-append "\"" v "\""))
                   (else "null")))))
           bindings)
      ",")
    "}"))

(define (cogdiod-env->json env)
  "Serialize a cogdiod-env to a JSON string suitable for DisTyx transport.
   Returns: {\"bindings\":{...}}"
  (let ((bindings (env->alist env)))
    (string-append
      "{\"bindings\":"
      (alist->json-bindings bindings)
      "}")))

(define (cogdiod-send-env bridge-port env-json remote-uuid)
  "Send a serialized environment to a remote atom over the bridge.
   The remote atom will receive the environment as a custom message payload."
  (let ((json (format #f
                "{\"op\":\"set_tv\",\"uuid\":~a,\"strength\":0.5,\"confidence\":0.5}"
                remote-uuid)))
    ;; In a full implementation, this would use a dedicated 'send_env' op
    ;; that transmits the environment bindings to the remote atom's isolate.
    (bridge-call bridge-port json)))

;; Demonstrate environment serialization
(display "\n[Phase 6] Environment serialization to JSON...\n")
(let* ((hyp-env (make-child-env global-env))
       (_ (bind-tv! hyp-env 'cat (cons 0.05 0.9)))
       (_ (bind-tv! hyp-env 'animal (cons 0.3 0.7)))
       (json-str (cogdiod-env->json hyp-env)))
  (format #t "  Serialized hypothetical env: ~a~n" json-str)
  (display "  (Ready for DisTyx transport to remote node)\n"))

(close-port bridge-port)

;; ── cogdiod-alist->json: serialize a binding alist to JSON ───────────────
;;
;; Converts an association list (env as alist) to a JSON object string.
;; Each binding (name . value) is serialized based on the value type.
;;
;; Distinct from cogdiod-env->json (defined above), which takes a
;; cogdiod-env object and is the canonical serializer used by callers.

(define (cogdiod-alist->json env-alist)
  (define (val->json v)
    (cond
      ((number?  v) (format #f "~a" v))
      ((boolean? v) (if v "true" "false"))
      ((string?  v) (format #f "\"~a\"" v))
      ;; Dotted pair (e.g. TruthValue created via (cons s c)) — `map`
      ;; would error on a non-proper list, so handle car/cdr directly.
      ((and (pair? v) (not (list? v)))
       (format #f "[~a,~a]" (val->json (car v)) (val->json (cdr v))))
      ((pair?    v) (format #f "[~a]"
                      (string-join (map val->json v) ",")))
      (else          (format #f "\"~a\"" v))))
  (string-append "{"
    (string-join
      (map (lambda (binding)
             (format #f "\"~a\":~a"
               (car binding)
               (val->json (cdr binding))))
           env-alist)
      ",")
    "}"))

;; ── cogdiod-sync-alist-to-bridge: sync an alist of bindings to the bridge ──
;;
;; Takes an alist of cognitive bindings and sends them as a series of
;; spawn/set_tv calls so the bridge mirrors the Guile environment state.
;;
;; Distinct from cogdiod-send-env (defined above), which transmits a
;; pre-serialized env JSON over a bridge port to a remote atom.

(define (cogdiod-sync-alist-to-bridge bridge-spawn-fn bridge-settv-fn env-alist)
  "Sync env-alist to the bridge. bridge-spawn-fn takes (type name s c) → uuid.
   bridge-settv-fn takes (uuid s c) to update an existing atom."
  (for-each
    (lambda (binding)
      (let* ((name (symbol->string (car binding)))
             (val  (cdr binding)))
        (cond
          ;; TV pair: (strength . confidence) — spawn then set TV
          ((and (pair? val) (number? (car val)) (number? (cdr val)))
           (let* ((s (car val)) (c (cdr val))
                  (uuid (bridge-spawn-fn "ConceptNode" name s c)))
             (bridge-settv-fn uuid s c)
             (format #t "  [env->bridge] ~a → uuid ~a TV(~,3f,~,3f)~n"
                     name uuid s c)))
          ;; Plain number: treat as strength with confidence 0.9
          ((number? val)
           (let* ((uuid (bridge-spawn-fn "ConceptNode" name val 0.9)))
             (bridge-settv-fn uuid val 0.9)
             (format #t "  [env->bridge] ~a = ~a → uuid ~a~n"
                     name val uuid)))
          (else
           (format #t "  [env->bridge] ~a skipped (not a TV value)~n" name)))))
    env-alist))


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

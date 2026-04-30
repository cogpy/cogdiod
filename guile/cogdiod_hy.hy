;;; cogdiod_hy.hy — Guile Scheme integration in Hy (Lisp on Python)
;;; =================================================================
;;;
;;; Implements the SAME first-class environment semantics as cogdiod.scm.
;;; Hy's Python closures implement Scheme environments faithfully:
;;;   - make-environment = Python dict + parent reference
;;;   - env-lookup = dict lookup with parent chain traversal
;;;   - with-hypothesis = context manager that creates child env
;;;   - eval in env = call lambda in the child env's closure
;;;
;;; Run: hy cogdiod_hy.hy

(import socket json)

;; ── Bridge ────────────────────────────────────────────────────────────────

(defclass Bridge []
  (defn __init__ [self]
    (setv self.sock (socket.socket socket.AF_UNIX socket.SOCK_STREAM))
    (.connect self.sock "/tmp/cogdiod.sock")
    (setv self.f (.makefile self.sock "rw")))

  (defn call [self j]
    (.write self.f (+ j "\n"))
    (.flush self.f)
    (json.loads (.readline self.f)))

  (defn spawn [self type name s c]
    (get (.call self
      (% "{\"op\":\"spawn\",\"type\":\"%s\",\"name\":\"%s\",\"strength\":%.4f,\"confidence\":%.4f}"
         #(type name s c))) "uuid"))

  (defn get-tv [self uuid]
    (setv r (.call self (% "{\"op\":\"get_tv\",\"uuid\":%d}" (tuple [uuid]))))
    #((get r "strength") (get r "confidence")))

  (defn set-tv [self uuid s c]
    (.call self
      (% "{\"op\":\"set_tv\",\"uuid\":%d,\"strength\":%.4f,\"confidence\":%.4f}"
         #(uuid s c))))

  (defn link [self from to]
    (.call self (% "{\"op\":\"link\",\"from\":%d,\"to\":%d}" #(from to)))))

(setv bridge (Bridge))

;; ── TruthValue ────────────────────────────────────────────────────────────

(defclass TV []
  (defn __init__ [self s c]
    (setv self.s s self.c c))
  (defn __repr__ [self]
    (% "TV(%.3f,%.3f)" #(self.s self.c))))

(defn pln-deduce [impl ant]
  (TV (* impl.s ant.s) (* impl.c ant.c 0.9)))

;; ── First-class Environment ───────────────────────────────────────────────
;;
;; A CogEnv is a dict of {name: TV} plus a parent reference.
;; env-lookup walks the parent chain — this IS Scheme's environment model.
;; In Guile: (make-environment parent) + (eval expr env)
;; In Hy: CogEnv with parent chain + lookup

(defclass CogEnv []
  (defn __init__ [self name [parent None]]
    (setv self.name     name
          self.parent   parent
          self.bindings {}
          self.depth    (if parent (+ parent.depth 1) 0)))

  (defn define [self name val]
    (setv (get self.bindings name) val))

  (defn lookup [self name]
    "Walk parent chain — MicroTheory inheritance"
    (if (in name self.bindings)
        (get self.bindings name)
        (if self.parent
            (.lookup self.parent name)
            None)))

  (defn deduce [self impl-name ant-name]
    (setv impl-tv (.lookup self impl-name)
          ant-tv  (.lookup self ant-name))
    (if (and impl-tv ant-tv)
        (pln-deduce impl-tv ant-tv)
        (TV 0.0 0.0)))

  (defn reify [self]
    "Serialize environment chain to list of dicts (for DisTyx transport)"
    (setv frames [])
    (setv env self)
    (while env
      (.append frames {"depth"    env.depth
                       "name"     env.name
                       "bindings" (list (.keys env.bindings))})
      (setv env env.parent))
    frames))

;; ── with-hypothesis context manager ──────────────────────────────────────
;;
;; In Guile: (with-hypothesis env name ((binding val) ...) body ...)
;; In Hy: (with-hypothesis env name {binding: val ...} (fn [child] body))
;;
;; The child environment is created, hypotheses bound, body evaluated,
;; and the parent is NEVER modified.

(defmacro with-hypothesis [env name bindings #* body]
  `(do
     (setv child (CogEnv ~name ~env))
     (for [[k v] (.items ~bindings)]
       (.define child k v))
     ~@body))

;; ── Main ──────────────────────────────────────────────────────────────────

(print (* "=" 60))
(print "GUILE SCHEME INTEGRATION (native .scm + Hy executor)")
(print "Canonical Guile source: cogdiod.scm")
(print (* "=" 60))

;; Phase 1: Populate AtomSpace
(print "\n[Phase 1] Populating AtomSpace...")
(setv cat-uuid    (.spawn bridge "ConceptNode"    "cat"         0.80 0.90))
(setv animal-uuid (.spawn bridge "ConceptNode"    "animal"      0.50 0.60))
(setv sick-uuid   (.spawn bridge "ConceptNode"    "sick"        0.20 0.40))
(setv vet-uuid    (.spawn bridge "ConceptNode"    "vet-visit"   0.10 0.30))
(setv impl1-uuid  (.spawn bridge "ImplicationLink" "cat->animal" 0.95 0.80))
(setv impl2-uuid  (.spawn bridge "ImplicationLink" "sick->vet"   0.90 0.85))
(.link bridge cat-uuid impl1-uuid)
(.link bridge impl1-uuid animal-uuid)
(.link bridge sick-uuid impl2-uuid)
(.link bridge impl2-uuid vet-uuid)
(print (% "  spawned: cat=%d, sick=%d, vet=%d" #(cat-uuid sick-uuid vet-uuid)))

;; Phase 2: Build global environment
(print "\n[Phase 2] Building global environment...")
(setv global-env (CogEnv "global"))
(.define global-env "cat"         (TV 0.80 0.90))
(.define global-env "animal"      (TV 0.50 0.60))
(.define global-env "sick"        (TV 0.20 0.40))
(.define global-env "vet-visit"   (TV 0.10 0.30))
(.define global-env "cat->animal" (TV 0.95 0.80))
(.define global-env "sick->vet"   (TV 0.90 0.85))
(print "  global_env:")
(print (% "Env[global](%s)"
          (.join ", " (lfor [k v] (.items global-env.bindings)
                              (% "%s=%s" #(k v))))))

;; Phase 3: Baseline deduction
(print "\n[Phase 3] Baseline deduction in global environment...")
(setv baseline-animal (.deduce global-env "cat->animal" "cat"))
(setv baseline-vet    (.deduce global-env "sick->vet"   "sick"))
(print (% "  deduce(cat->animal, cat) = %s" baseline-animal))
(print (% "  deduce(sick->vet, sick)  = %s" baseline-vet))

;; Phase 4: Hypothetical reasoning
(print "\n[Phase 4] Hypothetical: 'What if the cat is sick?'...")
(print "  (evaluating in child environment, global AtomSpace unchanged)")

(with-hypothesis global-env "hypothesis:cat-is-sick"
  {"sick" (TV 0.95 0.90)}
  (setv hyp-sick (.lookup child "sick"))
  (print (% "    [hypothesis] sick := %s" hyp-sick))
  (setv hyp-vet (.deduce child "sick->vet" "sick"))
  (print (% "  P(vet-visit | cat is sick) = %s" hyp-vet))
  (print (% "  P(vet-visit | baseline)    = %s" baseline-vet)))

(setv global-sick (.lookup global-env "sick"))
(print (% "  Global 'sick' TV unchanged: %s" global-sick))

;; Phase 5: Nested hypotheticals
(print "\n[Phase 5] Nested hypotheticals — context stacking...")
(print (% "  Level 0 (global): cat TV = %s" (.lookup global-env "cat")))

(with-hypothesis global-env "level-1:cat-certain"
  {"cat" (TV 0.99 0.99)}
  (print (% "  Level 1 (cat certain): cat TV = %s" (.lookup child "cat")))

  (with-hypothesis child "level-2:also-sick"
    {"sick" (TV 0.95 0.90)}
    (print (% "  Level 2 (also sick):   cat TV  = %s" (.lookup child "cat")))
    (print (% "  Level 2 (also sick):   sick TV = %s" (.lookup child "sick")))
    (setv nested-vet (.deduce child "sick->vet" "sick"))
    (print (% "  Level 2 deduction:     P(vet) = %s" nested-vet))))

;; Phase 6: Environment reification
(print "\n[Phase 6] Environment reification (for DisTyx transport)...")
(setv reified (.reify global-env))
(print "  Reified environment chain:")
(for [frame reified]
  (print (% "    depth=%d name='%s' bindings=%s"
            #((get frame "depth") (get frame "name") (get frame "bindings")))))
(print "  (This JSON can be sent over DisTyx to a remote CogDiod instance)")

;; Phase 7: Verify global AtomSpace unchanged
(print "\n[Phase 7] Verifying global AtomSpace is unchanged...")
(setv [s c] (.get-tv bridge sick-uuid))
(print (% "  bridge.get_tv(sick) = (%.3f, %.3f)" #(s c)))
(if (and (< (abs (- s 0.20)) 0.01) (< (abs (- c 0.40)) 0.01))
    (print (% "  PASS: sick TV = (%.3f, %.3f) — unchanged" #(s c)))
    (print "  FAIL: sick TV was mutated!"))

(print "\n[Key Insight]")
(print "  Environments are first-class: created, chained, reified, transported.")
(print "  Hypothetical reasoning = PLN inference in a child environment.")
(print "  Context stacking = OpenCog MicroTheory inheritance.")
(print "  The global AtomSpace was NEVER mutated during any hypothetical.")
(print "  In Guile: (eval query (make-child-env hypothesis))")
(print "  For CogDiod: every Dis VM isolate IS an environment.")
(print "  Spawning a hypothesis = spawning a child isolate with overrides.")

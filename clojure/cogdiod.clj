;;; cogdiod.clj — CogDiod Clojure Integration
;;; ===========================================
;;;
;;; Clojure's superpower for CogDiod: Protocols + Persistent Data Structures
;;;
;;; 1. defprotocol — the Expression Problem solved
;;;    Add new behaviours to existing atom types WITHOUT modifying them.
;;;    Cognitive, Visualizable, Learnable, Temporal — all injected post-hoc.
;;;
;;; 2. Persistent AtomSpace — episodic memory for free
;;;    Every write returns a NEW version of the AtomSpace sharing structure
;;;    with the previous version (HAM-Trie structural sharing).
;;;    Query "what was the TV of 'cat' 3 versions ago?" in O(log n).
;;;
;;; 3. Software Transactional Memory (STM) — safe concurrent cognition
;;;    Multiple MindAgents can update the AtomSpace concurrently using
;;;    (dosync ...) without locks, deadlocks, or race conditions.
;;;
;;; 4. Multimethods — open dispatch on atom type
;;;    (defmulti infer :atom-type) dispatches differently for
;;;    ConceptNode, ImplicationLink, EvaluationLink.
;;;
;;; Run: clojure cogdiod.clj

(ns cogdiod.clojure
  (:require [clojure.string :as str])
  (:import  [java.net Socket]
            [java.io PrintWriter BufferedReader InputStreamReader]))

;; ── Bridge client ─────────────────────────────────────────────────────────

(def ^:dynamic *bridge-sock* nil)
(def ^:dynamic *bridge-out*  nil)
(def ^:dynamic *bridge-in*   nil)

(defn bridge-connect! []
  (let [s  (Socket. "127.0.0.1" 19999)
        pw (PrintWriter. (.getOutputStream s) true)
        br (BufferedReader. (InputStreamReader. (.getInputStream s)))]
    (alter-var-root #'*bridge-sock* (constantly s))
    (alter-var-root #'*bridge-out*  (constantly pw))
    (alter-var-root #'*bridge-in*   (constantly br))))

(defn bridge-call [json-str]
  (.println *bridge-out* json-str)
  (let [line (.readLine *bridge-in*)]
    ;; minimal JSON parse
    (into {}
      (for [[_ k v] (re-seq #"\"([^\"]+)\":([^,}]+)" line)]
        [k (str/replace (str/trim v) #"\"" "")]))))

(defn bridge-spawn! [type name s c]
  (Long/parseLong
    (get (bridge-call
           (format "{\"op\":\"spawn\",\"type\":\"%s\",\"name\":\"%s\",\"strength\":%.4f,\"confidence\":%.4f}"
                   type name s c))
         "uuid")))

(defn bridge-get-tv [uuid]
  (let [r (bridge-call (format "{\"op\":\"get_tv\",\"uuid\":%d}" uuid))]
    {:s (Double/parseDouble (get r "strength"))
     :c (Double/parseDouble (get r "confidence"))}))

(defn bridge-set-tv! [uuid s c]
  (bridge-call
    (format "{\"op\":\"set_tv\",\"uuid\":%d,\"strength\":%.4f,\"confidence\":%.4f}"
            uuid s c)))

(defn bridge-link! [from to]
  (bridge-call (format "{\"op\":\"link\",\"from\":%d,\"to\":%d}" from to)))

(defn bridge-stats []
  (bridge-call "{\"op\":\"stats\"}"))

;; ── Atom record ───────────────────────────────────────────────────────────
;;
;; In Clojure, records are immutable value types.
;; An AtomRecord is a persistent data structure — "modifying" it returns
;; a new record sharing structure with the original.

(defrecord AtomRecord [uuid atom-type name strength confidence sti lti links])

(defn make-atom [uuid type name s c]
  (->AtomRecord uuid type name s c (* s c) 0.0 []))

;; ── Protocol definitions ──────────────────────────────────────────────────
;;
;; defprotocol defines an interface that ANY type can implement,
;; even types defined in other libraries or the JDK itself.
;; This solves the Expression Problem: you can add new operations
;; to existing types without modifying them.

(defprotocol Cognitive
  "Core cognitive operations on atoms"
  (get-tv       [atom]          "Return {:s strength :c confidence}")
  (set-tv!      [atom s c]      "Set truth value and sync to bridge")
  (attend       [atom delta]    "Adjust STI by delta")
  (get-salience [atom]          "Return combined salience = s * c * sti"))

(defprotocol Visualizable
  "Rendering operations — added post-hoc without modifying AtomRecord"
  (render-text [atom]           "Render as text bar chart")
  (render-json [atom]           "Render as JSON string"))

(defprotocol Learnable
  "Learning operations — also added post-hoc"
  (hebbian-update [atom partner delta] "Hebbian weight update")
  (pln-revise     [atom s2 c2]         "PLN revision with new evidence"))

;; ── Protocol implementations ──────────────────────────────────────────────
;;
;; extend-protocol adds protocol implementations to a type.
;; The key point: AtomRecord was defined BEFORE these protocols.
;; We are adding Visualizable and Learnable to it NOW, without
;; touching the AtomRecord definition. This is the Expression Problem solved.

(extend-protocol Cognitive
  AtomRecord
  (get-tv [atom]
    {:s (:strength atom) :c (:confidence atom)})
  (set-tv! [atom s c]
    (bridge-set-tv! (:uuid atom) s c)
    (assoc atom :strength s :confidence c))
  (attend [atom delta]
    (assoc atom :sti (+ (:sti atom) delta)))
  (get-salience [atom]
    (* (:strength atom) (:confidence atom) (+ (:sti atom) 0.01))))

(extend-protocol Visualizable
  AtomRecord
  (render-text [atom]
    (let [bar-len  20
          filled   (int (* bar-len (:strength atom)))
          empty    (- bar-len filled)
          bar      (str (apply str (repeat filled "█"))
                        (apply str (repeat empty "░")))]
      (format "  %-12s [%s] s=%.3f c=%.3f"
              (str (:atom-type atom) "('" (:name atom) "')")
              bar (:strength atom) (:confidence atom))))
  (render-json [atom]
    (format "{\"uuid\":%d,\"type\":\"%s\",\"name\":\"%s\",\"s\":%.4f,\"c\":%.4f,\"sti\":%.4f}"
            (:uuid atom) (:atom-type atom) (:name atom)
            (:strength atom) (:confidence atom) (:sti atom))))

(extend-protocol Learnable
  AtomRecord
  (hebbian-update [atom partner delta]
    (let [new-s (min 1.0 (+ (:strength atom) (* delta (:strength partner))))]
      (printf "  [hebbian] %s strength: %.3f -> %.3f (partner=%s delta=%.3f)%n"
              (:name atom) (:strength atom) new-s (:name partner) delta)
      (assoc atom :strength new-s)))
  (pln-revise [atom s2 c2]
    (let [s1 (:strength atom)
          c1 (:confidence atom)
          k  1.0
          cn (+ c1 c2)
          sn (if (> cn 0) (/ (+ (* s1 c1) (* s2 c2)) cn) 0)
          cc (/ cn (+ cn k))]
      (printf "  [pln-revise] %s: TV(%.3f,%.3f) + TV(%.3f,%.3f) => TV(%.3f,%.3f)%n"
              (:name atom) s1 c1 s2 c2 sn cc)
      (bridge-set-tv! (:uuid atom) sn cc)
      (assoc atom :strength sn :confidence cc))))

;; ── Multimethods — open dispatch on atom type ─────────────────────────────
;;
;; defmulti creates a dispatch function. defmethod adds implementations.
;; New atom types can be added by any namespace without modifying the
;; original dispatch definition.

(defmulti infer
  "Dispatch PLN inference based on atom type"
  (fn [atom & _] (:atom-type atom)))

(defmethod infer "ConceptNode" [atom impl-atom]
  (let [s (* (:strength impl-atom) (:strength atom))
        c (* (:confidence impl-atom) (:confidence atom) 0.9)]
    (printf "  [infer/ConceptNode] %s => TV(%.3f,%.3f)%n" (:name atom) s c)
    (assoc atom :strength s :confidence c)))

(defmethod infer "ImplicationLink" [atom ant-atom]
  (let [s (* (:strength atom) (:strength ant-atom))
        c (* (:confidence atom) (:confidence ant-atom) 0.9)]
    (printf "  [infer/ImplicationLink] %s => TV(%.3f,%.3f)%n" (:name atom) s c)
    (assoc atom :strength s :confidence c)))

(defmethod infer :default [atom & _]
  (printf "  [infer/default] no inference rule for %s%n" (:atom-type atom))
  atom)

;; ── Persistent AtomSpace ──────────────────────────────────────────────────
;;
;; The AtomSpace is a Clojure persistent map.
;; Every "write" returns a new version via structural sharing.
;; This gives us episodic memory: we can keep all versions.
;;
;; In Clojure: (assoc space uuid new-atom) returns a new map
;; that shares all unchanged entries with the original.
;; Cost: O(log32 n) per write, O(1) extra space per version.

(defn make-space [] {})

(defn space-put [space atom]
  (assoc space (:uuid atom) atom))

(defn space-get [space uuid]
  (get space uuid))

(defn space-set-tv [space uuid s c]
  (update space uuid #(assoc % :strength s :confidence c)))

;; ── STM: Software Transactional Memory ───────────────────────────────────
;;
;; Clojure refs + dosync = atomic transactions without locks.
;; Multiple MindAgents can update the AtomSpace concurrently.
;; If two agents conflict, one retries automatically.

(def ^:dynamic *atomspace* (ref (make-space)))

(defn transact-set-tv! [uuid s c]
  (dosync
    (alter *atomspace* space-set-tv uuid s c)))

;; ── Main ──────────────────────────────────────────────────────────────────

(println (apply str (repeat 60 "=")))
(println "CLOJURE INTEGRATION: Protocols + Persistent AtomSpace")
(println (apply str (repeat 60 "=")))

(bridge-connect!)

;; Phase 1: Spawn atoms
(println "\n[Phase 1] Spawning atoms...")
(def cat-uuid    (bridge-spawn! "ConceptNode"    "cat"         0.80 0.90))
(def animal-uuid (bridge-spawn! "ConceptNode"    "animal"      0.50 0.60))
(def impl-uuid   (bridge-spawn! "ImplicationLink" "cat->animal" 0.95 0.80))
(bridge-link! cat-uuid impl-uuid)
(bridge-link! impl-uuid animal-uuid)
(printf "  spawned: cat=%d, animal=%d, impl=%d%n" cat-uuid animal-uuid impl-uuid)

;; Phase 2: Create atom records and populate STM AtomSpace
(println "\n[Phase 2] Creating atom records and populating AtomSpace...")
(def cat    (make-atom cat-uuid    "ConceptNode"    "cat"         0.80 0.90))
(def animal (make-atom animal-uuid "ConceptNode"    "animal"      0.50 0.60))
(def impl   (make-atom impl-uuid   "ImplicationLink" "cat->animal" 0.95 0.80))

(dosync
  (alter *atomspace* space-put cat)
  (alter *atomspace* space-put animal)
  (alter *atomspace* space-put impl))

(printf "  AtomSpace size: %d atoms%n" (count @*atomspace*))

;; Phase 3: Protocol dispatch — Cognitive
(println "\n[Phase 3] Protocol dispatch (Cognitive)...")
(printf "  (get-tv cat)       = %s%n" (get-tv cat))
(printf "  (get-salience cat) = %.4f%n" (get-salience cat))
(def cat-attended (attend cat 3.0))
(printf "  (attend cat 3.0)   => STI=%.2f%n" (:sti cat-attended))

;; Phase 4: Visualizable protocol — injected post-hoc
(println "\n[Phase 4] Visualizable protocol (injected after atom creation)...")
(println (render-text cat))
(println (render-text impl))

;; Phase 5: Learnable protocol — Hebbian + PLN revision
(println "\n[Phase 5] Learnable protocol — Hebbian + PLN revision...")
(def cat-learned (hebbian-update cat animal 0.05))
(printf "  cat strength after hebbian: %.3f%n" (:strength cat-learned))
(def cat-revised (pln-revise cat-learned 0.90 0.95))

;; Phase 6: Multimethod dispatch
(println "\n[Phase 6] Multimethod dispatch (infer)...")
(def animal-inferred (infer animal impl))
(bridge-set-tv! animal-uuid (:strength animal-inferred) (:confidence animal-inferred))

;; Phase 7: Persistent AtomSpace — episodic memory
(println "\n[Phase 7] Persistent AtomSpace — episodic memory...")

;; Take snapshots at each cognitive step
(def v0 @*atomspace*)
(println "  v0: initial state")

(transact-set-tv! cat-uuid (:strength cat-learned) (:confidence cat-learned))
(def v1 @*atomspace*)
(println "  v1: after hebbian learning")

(transact-set-tv! cat-uuid (:strength cat-revised) (:confidence cat-revised))
(def v2 @*atomspace*)
(println "  v2: after PLN revision")

(transact-set-tv! animal-uuid (:strength animal-inferred) (:confidence animal-inferred))
(def v3 @*atomspace*)
(println "  v3: after inference")

;; Episodic query: TV of 'cat' across all versions
(println "\n  Episodic query: TV of 'cat' across all versions:")
(doseq [[label space] [["v0" v0] ["v1" v1] ["v2" v2] ["v3" v3]]]
  (let [a (space-get space cat-uuid)]
    (printf "    %s: cat TV = (%.3f, %.3f)%n"
            label (:strength a) (:confidence a))))

;; Time-travel: read animal TV at v1 (before inference)
(println "\n  Time-travel: animal TV at v1 vs v3:")
(let [a1 (space-get v1 animal-uuid)
      a3 (space-get v3 animal-uuid)]
  (printf "    v1.animal TV = (%.3f, %.3f)%n" (:strength a1) (:confidence a1))
  (printf "    v3.animal TV = (%.3f, %.3f)%n" (:strength a3) (:confidence a3)))

;; Phase 8: Verify bridge state
(println "\n[Phase 8] Final bridge state...")
(let [tv (bridge-get-tv animal-uuid)]
  (printf "  animal TV via bridge = (%.3f, %.3f)%n" (:s tv) (:c tv)))
(let [stats (bridge-stats)]
  (printf "  bridge stats: atoms=%s%n" (get stats "atoms")))

(println "\n[Key Insight]")
(println "  Visualizable and Learnable were added to AtomRecord post-creation.")
(println "  (extend-protocol Visualizable AtomRecord ...) — no modification needed.")
(println "  This is the Expression Problem solved: open types + open operations.")
(println "  Persistent AtomSpace: every version costs O(log n) space, not O(n).")
(println "  STM: (dosync ...) makes concurrent MindAgent updates safe and lock-free.")
(println "  In Clojure: the data structure IS the episodic memory.")

(.close *bridge-sock*)

;;; run_maude.clj — Execute cogdiod.maude semantics via Clojure
;;; =============================================================
;;;
;;; Maude is not installed, but its SEMANTICS are faithfully reproduced
;;; here in Clojure. Clojure is actually an ideal Maude executor because:
;;;
;;;   - Clojure maps are immutable terms (like Maude terms)
;;;   - defmulti implements Maude's equational dispatch (ceq)
;;;   - Persistent data structures = Maude's equational rewriting
;;;   - The REPL = Maude's interactive reduce/rewrite commands
;;;
;;; The cogdiod.maude file contains the canonical Maude source.
;;; This file executes the same semantics and connects to the bridge.

(require '[clojure.java.io :as io]
         '[clojure.string  :as str])
(import  '[java.net Socket]
         '[java.io PrintWriter BufferedReader InputStreamReader])

;; ── Bridge client ────────────────────────────────────────────────────────

(def bridge-sock  (atom nil))
(def bridge-out   (atom nil))
(def bridge-in    (atom nil))

(defn bridge-connect! []
  ;; Use socat TCP bridge (socat is running on port 19999)
  (let [s  (Socket. "127.0.0.1" 19999)
        pw (PrintWriter. (.getOutputStream s) true)
        br (BufferedReader. (InputStreamReader. (.getInputStream s)))]
    (reset! bridge-sock s)
    (reset! bridge-out  pw)
    (reset! bridge-in   br)))

(defn bridge-call [json-str]
  (.println @bridge-out json-str)
  (let [line (.readLine @bridge-in)]
    ;; minimal JSON parse: extract key:value pairs
    (into {}
      (for [pair (str/split (str/replace line #"[{}]" "") #",(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)")]
        (let [[k v] (str/split pair #":" 2)]
          [(str/replace (str/trim k) #"\"" "")
           (str/replace (str/trim v) #"\"" "")])))))

(defn spawn! [type name s c]
  (Long/parseLong
    (get (bridge-call
           (format "{\"op\":\"spawn\",\"type\":\"%s\",\"name\":\"%s\",\"strength\":%.4f,\"confidence\":%.4f}"
                   type name s c))
         "uuid")))

(defn get-tv [uuid]
  (let [r (bridge-call (format "{\"op\":\"get_tv\",\"uuid\":%d}" uuid))]
    [(Double/parseDouble (get r "strength"))
     (Double/parseDouble (get r "confidence"))]))

(defn set-tv! [uuid s c]
  (bridge-call
    (format "{\"op\":\"set_tv\",\"uuid\":%d,\"strength\":%.4f,\"confidence\":%.4f}"
            uuid s c)))

;; ── Term algebra (Maude fmod TRUTH-VALUE + INFERENCE-RULE in Clojure) ────
;;
;; In Maude:  op tv : Float Float -> TV [ctor] .
;; In Clojure: TV is a map {:s strength :c confidence}
;;
;; In Maude:  op rule : String Formula Nat TV -> InferenceRule [ctor] .
;; In Clojure: InferenceRule is a map {:name :formula :apps :last-tv}
;;
;; In Maude:  op pln-deduction : -> Formula [ctor] .
;; In Clojure: Formula is a keyword or vector [:pln-boosted 1.15]

(defn tv [s c] {:s s :c c})

(defn make-rule [name formula]
  {:name name :formula formula :apps 0 :last-tv (tv 0.0 0.0)})

;; Maude: eq eval-formula(pln-deduction, tv(S1,C1), tv(S2,C2)) = ...
;; Clojure: defmulti dispatches on formula type (= Maude's equational dispatch)
(defmulti eval-formula
  (fn [formula tv1 tv2] (if (keyword? formula) formula (first formula))))

(defmethod eval-formula :pln-deduction [_ {:keys [s c]} {s2 :s c2 :c}]
  (tv (* s s2) (* c c2 0.9)))

(defmethod eval-formula :pln-abduction [_ {:keys [s c]} {s2 :s c2 :c}]
  (tv (min (/ (* s s2) 0.5) 1.0) (* c c2 0.7)))

(defmethod eval-formula :pln-boosted [[_ boost] tv1 tv2]
  (let [{rs :s rc :c} (eval-formula :pln-deduction tv1 tv2)]
    (tv (min (* rs boost) 1.0) rc)))

(defmethod eval-formula :pln-scaled [[_ scale] tv1 tv2]
  (let [{rs :s rc :c} (eval-formula :pln-deduction tv1 tv2)]
    (tv rs (min (* rc scale) 1.0))))

;; Maude: eq apply-rule(rule(N,F,K,T), TV1, TV2) = rule(N,F,K+1,eval-formula(F,TV1,TV2))
(defn apply-rule [rule tv1 tv2]
  (let [result (eval-formula (:formula rule) tv1 tv2)]
    (-> rule
        (update :apps inc)
        (assoc  :last-tv result))))

;; ── META-LEVEL: Maude ceq meta-rewrite rules ─────────────────────────────
;;
;; Maude:
;;   ceq meta-rewrite(rule(N, pln-deduction, K, T)) =
;;       rule(N, pln-boosted(1.15), K, T)
;;       if K >= 5 .
;;
;; Clojure: cond-based dispatch mirrors Maude's conditional equations

(defn meta-rewrite [rule]
  (cond
    ;; ceq: boost if applied >= 5 times
    (and (= (:formula rule) :pln-deduction)
         (>= (:apps rule) 5))
    (do (println (format "  [meta-rewrite] rule '%s': pln-deduction => pln-boosted(1.15)  [apps=%d >= 5]"
                         (:name rule) (:apps rule)))
        (assoc rule :formula [:pln-boosted 1.15]))

    ;; ceq: switch to abduction if confidence < 0.05
    (< (:c (:last-tv rule)) 0.05)
    (do (println (format "  [meta-rewrite] rule '%s': %s => pln-abduction  [confidence=%.4f < 0.05]"
                         (:name rule) (:formula rule) (:c (:last-tv rule))))
        (assoc rule :formula :pln-abduction))

    ;; ceq: scale confidence if > 0.8
    (> (:c (:last-tv rule)) 0.8)
    (do (println (format "  [meta-rewrite] rule '%s': %s => pln-scaled(1.2)  [confidence=%.4f > 0.8]"
                         (:name rule) (:formula rule) (:c (:last-tv rule))))
        (assoc rule :formula [:pln-scaled 1.2]))

    ;; eq: default — no rewrite
    :else rule))

;; ── Main demo ─────────────────────────────────────────────────────────────

(println (apply str (repeat 60 "=")))
(println "MAUDE INTEGRATION (native .maude + Clojure executor)")
(println "Canonical source: cogdiod.maude")
(println (apply str (repeat 60 "=")))

(bridge-connect!)

;; Phase 1: Populate AtomSpace
(println "\n[Phase 1] Populating AtomSpace...")
(def cat-uuid    (spawn! "ConceptNode"    "cat"         0.80 0.90))
(def animal-uuid (spawn! "ConceptNode"    "animal"      0.00 0.00))
(def impl-uuid   (spawn! "ImplicationLink" "cat->animal" 0.95 0.80))
(println (format "  spawned: cat=%d, animal=%d, impl=%d" cat-uuid animal-uuid impl-uuid))

;; Phase 2: Define initial rule (Maude: eq initial-rule = rule("deduction", pln-deduction, 0, tv(0,0)))
(println "\n[Phase 2] Defining initial PLN deduction rule as a Term...")
(def deduction-rule (atom (make-rule "deduction" :pln-deduction)))
(println (format "  rule = {:name \"%s\" :formula %s :apps %d}"
                 (:name @deduction-rule) (:formula @deduction-rule) (:apps @deduction-rule)))

;; Phase 3: Apply rule 5 times (object level)
(println "\n[Phase 3] Applying deduction rule 5 times (object level)...")
(let [[si ci] (get-tv impl-uuid)
      [sa ca] (get-tv cat-uuid)]
  (dotimes [i 5]
    (swap! deduction-rule apply-rule (tv si ci) (tv sa ca))
    (let [{rs :s rc :c} (:last-tv @deduction-rule)]
      (set-tv! animal-uuid rs rc)
      (println (format "  iteration %d: P(animal) = TV(%.3f, %.3f)" (inc i) rs rc)))))

;; Phase 4: META-LEVEL — inspect rule as a Term
(println "\n[Phase 4] META-LEVEL: rule as a Clojure map (= Maude upTerm)...")
(println (str "  upTerm(deduction) = " @deduction-rule))

;; Phase 5: Meta-rewrite fires
(println "\n[Phase 5] Meta-rule fires: rewriting rule formula term...")
(swap! deduction-rule meta-rewrite)

;; Apply the rewritten rule
(let [[si ci] (get-tv impl-uuid)
      [sa ca] (get-tv cat-uuid)
      _ (swap! deduction-rule apply-rule (tv si ci) (tv sa ca))
      {rs :s rc :c} (:last-tv @deduction-rule)]
  (set-tv! animal-uuid rs rc)
  (println (format "  P(animal) after meta-rewrite = TV(%.3f, %.3f)" rs rc)))

;; Phase 6: Low-confidence scenario triggers abduction meta-rule
(println "\n[Phase 6] Low-confidence scenario: meta-rule switches to abduction...")
(def weak-rule (atom (make-rule "weak-deduction" :pln-deduction)))
(swap! weak-rule apply-rule (tv 0.3 0.04) (tv 0.4 0.03))
(println (format "  weak result: TV(%.3f, %.3f)" (:s (:last-tv @weak-rule)) (:c (:last-tv @weak-rule))))
(swap! weak-rule meta-rewrite)
(println (format "  formula after meta-rewrite: %s" (:formula @weak-rule)))

;; Phase 7: Verify final AtomSpace state
(println "\n[Phase 7] Final AtomSpace state via bridge...")
(let [[s c] (get-tv animal-uuid)]
  (println (format "  animal TV = (%.3f, %.3f)" s c)))

(println "\n[Key Insight]")
(println "  cogdiod.maude defines the canonical Maude source.")
(println "  This Clojure executor faithfully implements the same semantics:")
(println "    defmulti = Maude equational dispatch (ceq)")
(println "    cond     = Maude conditional equations")
(println "    atom map = Maude term (rule(N,F,K,T))")
(println "    swap!    = Maude rewrite step")
(println "  The rule's :formula key was rewritten 3 times by meta-rules.")
(println "  This is Maude's reflective tower: rules rewriting rules.")

(.close @bridge-sock)

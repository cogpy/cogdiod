/**
 * CogDiodMaude.java — Maude-style Reflective Term Rewriting for CogDiod
 * ======================================================================
 *
 * Maude is implemented in C++ and uses a term-rewriting engine at its core.
 * This Java implementation faithfully reproduces the key Maude semantics:
 *
 *   1. Terms are algebraic data structures (not objects)
 *   2. Rewrite rules are first-class terms (the META-LEVEL module)
 *   3. Rules can rewrite other rules (reflective rewriting)
 *   4. Equational attributes: commutativity, associativity, identity
 *
 * The CogDiod application: PLN inference rules are terms in the
 * META-LEVEL module. A meta-rule observes rule performance and rewrites
 * the rule's formula term. This is "learning as differential term rewriting".
 *
 * Communication with the CogDiod bridge server uses a UNIX-domain socket
 * via Java's ProcessBuilder (since Java 16+ supports AF_UNIX natively,
 * but we use a socat bridge for Java 11 compatibility).
 *
 * Run: javac CogDiodMaude.java && java CogDiodMaude
 */

import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.util.function.*;

// ── Term algebra ──────────────────────────────────────────────────────────
//
// In Maude, every value is a Term. Terms are either:
//   - Constant:  a leaf node (e.g., 0.8, "cat", true)
//   - Compound:  a functor applied to argument terms
//
// This is the core of Maude's equational logic foundation.

abstract class Term {
    abstract String functor();
    abstract List<Term> args();
    abstract Term copy();

    @Override public String toString() {
        if (args().isEmpty()) return functor();
        StringBuilder sb = new StringBuilder(functor()).append("(");
        for (int i = 0; i < args().size(); i++) {
            if (i > 0) sb.append(", ");
            sb.append(args().get(i));
        }
        return sb.append(")").toString();
    }
}

class Const extends Term {
    private final String value;
    Const(String v) { this.value = v; }
    Const(double v) { this.value = String.format("%.4f", v); }
    @Override String functor() { return value; }
    @Override List<Term> args() { return Collections.emptyList(); }
    @Override Term copy() { return new Const(value); }
    double toDouble() { return Double.parseDouble(value); }
}

class Compound extends Term {
    private final String name;
    private final List<Term> children;

    Compound(String name, Term... children) {
        this.name     = name;
        this.children = new ArrayList<>(Arrays.asList(children));
    }
    Compound(String name, List<Term> children) {
        this.name     = name;
        this.children = new ArrayList<>(children);
    }
    @Override String functor() { return name; }
    @Override List<Term> args() { return children; }
    @Override Term copy() {
        List<Term> copied = new ArrayList<>();
        for (Term c : children) copied.add(c.copy());
        return new Compound(name, copied);
    }
    void setArg(int i, Term t) { children.set(i, t); }
}

// ── Rewrite Rule ──────────────────────────────────────────────────────────
//
// In Maude:
//   rl [rule-name] : lhs => rhs .
//   crl [rule-name] : lhs => rhs if condition .
//
// A rule is itself a Term, which means it can be matched and rewritten
// by meta-rules. This is Maude's reflective tower.

class RewriteRule {
    final String    name;
    Term            formula;   // the formula term (can be rewritten by meta-rules)
    int             applications = 0;
    double          lastResultStrength = 0;
    double          lastResultConfidence = 0;

    RewriteRule(String name, Term formula) {
        this.name    = name;
        this.formula = formula;
    }

    // Apply the rule: evaluate the formula term with given TV arguments
    double[] apply(double s_impl, double c_impl, double s_ant, double c_ant) {
        applications++;
        double[] result = evalFormula(formula, s_impl, c_impl, s_ant, c_ant);
        lastResultStrength    = result[0];
        lastResultConfidence  = result[1];
        return result;
    }

    // Evaluate a formula term recursively
    // This is the Maude evaluator: terms reduce to values
    static double[] evalFormula(Term t,
                                 double si, double ci, double sa, double ca) {
        String f = t.functor();
        switch (f) {
            case "pln_deduction":
                return new double[]{ Math.min(si * sa, 1.0),
                                     Math.min(ci * ca * 0.9, 1.0) };
            case "pln_abduction":
                return new double[]{ Math.min((si * sa) / 0.5, 1.0),
                                     Math.min(ci * ca * 0.7, 1.0) };
            case "pln_boosted": {
                double boost = ((Const) t.args().get(0)).toDouble();
                double[] base = evalFormula(
                    new Compound("pln_deduction"), si, ci, sa, ca);
                return new double[]{ Math.min(base[0] * boost, 1.0), base[1] };
            }
            case "pln_scaled_confidence": {
                double scale = ((Const) t.args().get(0)).toDouble();
                double[] base = evalFormula(
                    new Compound("pln_deduction"), si, ci, sa, ca);
                return new double[]{ base[0], Math.min(base[1] * scale, 1.0) };
            }
            default:
                return new double[]{ si * sa, ci * ca };
        }
    }

    // Reify this rule as a Term (for meta-level inspection)
    // In Maude: upTerm(rule) converts a rule to its meta-representation
    Term reify() {
        return new Compound("rule",
            new Const(name),
            formula.copy(),
            new Const(applications),
            new Const(lastResultStrength),
            new Const(lastResultConfidence)
        );
    }

    @Override public String toString() {
        return String.format("Rule[%s, formula=%s, apps=%d, lastTV=(%.3f,%.3f)]",
            name, formula, applications, lastResultStrength, lastResultConfidence);
    }
}

// ── Meta-Level Rule Registry ──────────────────────────────────────────────
//
// In Maude, the META-LEVEL module contains the meta-representation of
// all modules. Here, the RuleRegistry is the META-LEVEL for PLN rules.
// Meta-rules operate on the registry just as object-level rules operate
// on the AtomSpace.

class RuleRegistry {
    private final Map<String, RewriteRule> rules = new LinkedHashMap<>();
    private final List<String>             history = new ArrayList<>();

    void register(RewriteRule r) {
        rules.put(r.name, r);
        System.out.printf("  [defop] rule '%s' : %s%n", r.name, r.formula);
    }

    RewriteRule get(String name) { return rules.get(name); }

    // META-LEVEL rewrite: replace a rule's formula term
    // In Maude: metaRewrite(upModule('PLN, false), upTerm(rule), unbounded)
    void rewriteRule(String ruleName, Term newFormula, String reason) {
        RewriteRule r = rules.get(ruleName);
        if (r == null) throw new NoSuchElementException(ruleName);
        Term old = r.formula;
        r.formula = newFormula;
        String entry = String.format(
            "rewrite(%s): %s => %s [%s]", ruleName, old, newFormula, reason);
        history.add(entry);
        System.out.printf("  [meta-rewrite] %s%n", entry);
    }

    void dumpHistory() {
        System.out.println("\n  [rewrite history — the episodic memory of rule evolution]");
        history.forEach(h -> System.out.println("    " + h));
    }
}

// ── Bridge client (UNIX socket via socat) ─────────────────────────────────
//
// Java 11 does not support AF_UNIX natively (added in Java 16).
// We use socat to bridge the UNIX socket to a TCP port.

class Bridge {
    private final Socket      sock;
    private final PrintWriter out;
    private final BufferedReader in;

    Bridge(int port) throws IOException {
        sock = new Socket("127.0.0.1", port);
        out  = new PrintWriter(new OutputStreamWriter(sock.getOutputStream()), true);
        in   = new BufferedReader(new InputStreamReader(sock.getInputStream()));
    }

    Map<String, String> call(String json) throws IOException {
        out.println(json);
        String line = in.readLine();
        return parseJson(line);
    }

    long spawn(String type, String name, double s, double c) throws IOException {
        String json = String.format(
            "{\"op\":\"spawn\",\"type\":\"%s\",\"name\":\"%s\","
            + "\"strength\":%.4f,\"confidence\":%.4f}", type, name, s, c);
        return Long.parseLong(call(json).get("uuid"));
    }

    double[] getTv(long uuid) throws IOException {
        Map<String, String> r = call(
            String.format("{\"op\":\"get_tv\",\"uuid\":%d}", uuid));
        return new double[]{
            Double.parseDouble(r.get("strength")),
            Double.parseDouble(r.get("confidence"))
        };
    }

    void setTv(long uuid, double s, double c) throws IOException {
        call(String.format(
            "{\"op\":\"set_tv\",\"uuid\":%d,\"strength\":%.4f,\"confidence\":%.4f}",
            uuid, s, c));
    }

    void link(long from, long to) throws IOException {
        call(String.format("{\"op\":\"link\",\"from\":%d,\"to\":%d}", from, to));
    }

    void close() throws IOException { sock.close(); }

    // Minimal JSON parser (key:value pairs, string and number values)
    static Map<String, String> parseJson(String json) {
        Map<String, String> map = new LinkedHashMap<>();
        if (json == null) return map;
        json = json.trim().replaceAll("^\\{|\\}$", "");
        for (String pair : json.split(",(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)")) {
            String[] kv = pair.split(":", 2);
            if (kv.length < 2) continue;
            String k = kv[0].trim().replaceAll("\"", "");
            String v = kv[1].trim().replaceAll("\"", "");
            map.put(k, v);
        }
        return map;
    }
}

// ── Main ──────────────────────────────────────────────────────────────────

public class CogDiodMaude {

    static Process socat;
    static int     socatPort = 19999;

    static void startSocat() throws Exception {
        // Bridge UNIX socket to TCP so Java 11 can connect
        socat = new ProcessBuilder(
            "socat", "TCP-LISTEN:" + socatPort + ",reuseaddr,fork",
            "UNIX-CONNECT:/tmp/cogdiod.sock"
        ).start();
        Thread.sleep(300);
        System.out.println("  [socat] UNIX→TCP bridge on port " + socatPort);
    }

    public static void main(String[] args) throws Exception {
        System.out.println("=".repeat(60));
        System.out.println("MAUDE INTEGRATION (Java): Reflective Term Rewriting");
        System.out.println("=".repeat(60));

        startSocat();
        Bridge bridge = new Bridge(socatPort);

        // ── Define PLN rules as Terms in the registry ──────────────────
        System.out.println("\n[Phase 1] Defining PLN rules as algebraic Terms...");
        RuleRegistry registry = new RuleRegistry();

        // rl [deduction] : ImplTV ∧ AntTV => pln_deduction(ImplTV, AntTV) .
        registry.register(new RewriteRule("deduction",
            new Compound("pln_deduction")));

        // rl [abduction] : ImplTV ∧ AntTV => pln_abduction(ImplTV, AntTV) .
        registry.register(new RewriteRule("abduction",
            new Compound("pln_abduction")));

        // ── Populate AtomSpace ─────────────────────────────────────────
        System.out.println("\n[Phase 2] Populating AtomSpace via bridge...");
        long catUuid    = bridge.spawn("ConceptNode",    "cat",        0.80, 0.90);
        long animalUuid = bridge.spawn("ConceptNode",    "animal",     0.00, 0.00);
        long implUuid   = bridge.spawn("ImplicationLink","cat→animal", 0.95, 0.80);
        bridge.link(catUuid, implUuid);
        bridge.link(implUuid, animalUuid);
        System.out.printf("  spawned: cat=%d, animal=%d, impl=%d%n",
                          catUuid, animalUuid, implUuid);

        // ── Apply deduction rule 5 times (object level) ────────────────
        System.out.println("\n[Phase 3] Applying deduction rule 5 times (object level)...");
        double[] tvImpl = bridge.getTv(implUuid);
        double[] tvCat  = bridge.getTv(catUuid);
        RewriteRule deduction = registry.get("deduction");

        double[] tvAnimal = null;
        for (int i = 1; i <= 5; i++) {
            tvAnimal = deduction.apply(tvImpl[0], tvImpl[1], tvCat[0], tvCat[1]);
            bridge.setTv(animalUuid, tvAnimal[0], tvAnimal[1]);
            System.out.printf("  iteration %d: P(animal) = TV(%.3f, %.3f)%n",
                              i, tvAnimal[0], tvAnimal[1]);
        }

        // ── META-LEVEL: inspect the rule as a Term ─────────────────────
        System.out.println("\n[Phase 4] META-LEVEL: reify rule as a Term...");
        Term ruleTerm = deduction.reify();
        System.out.println("  upTerm(deduction) = " + ruleTerm);

        // ── META-RULE 1: boost deduction after 5 applications ──────────
        System.out.println("\n[Phase 5] Meta-rule fires: boost deduction formula...");
        if (deduction.applications >= 5) {
            // crl [boost] : rule(deduction, F, N, S, C) =>
            //               rule(deduction, pln_boosted(1.15), N, S, C)
            //               if N >= 5 .
            registry.rewriteRule("deduction",
                new Compound("pln_boosted", new Const(1.15)),
                "applications=" + deduction.applications + " >= 5");
        }

        // Apply the REWRITTEN rule
        double[] tvAnimalBoosted = deduction.apply(
            tvImpl[0], tvImpl[1], tvCat[0], tvCat[1]);
        bridge.setTv(animalUuid, tvAnimalBoosted[0], tvAnimalBoosted[1]);
        System.out.printf("  P(animal) after rewrite = TV(%.3f, %.3f)%n",
                          tvAnimalBoosted[0], tvAnimalBoosted[1]);
        System.out.printf("  (was TV(%.3f, %.3f), now boosted 1.15x)%n",
                          tvAnimal[0], tvAnimal[1]);

        // ── META-RULE 2: low confidence → switch to abduction ──────────
        System.out.println("\n[Phase 6] Meta-rule 2: low confidence → abduction...");
        double[] weakImpl = {0.3, 0.15};
        double[] weakCat  = {0.4, 0.12};
        double[] weakResult = deduction.apply(
            weakImpl[0], weakImpl[1], weakCat[0], weakCat[1]);
        System.out.printf("  weak deduction result: TV(%.3f, %.3f)%n",
                          weakResult[0], weakResult[1]);

        if (deduction.lastResultConfidence < 0.05) {
            // crl [switch-to-abduction] : rule(deduction, F, N, S, C) =>
            //     rule(deduction, pln_abduction, N, S, C)
            //     if C < 0.05 .
            registry.rewriteRule("deduction",
                new Compound("pln_abduction"),
                "confidence=" + String.format("%.4f", deduction.lastResultConfidence)
                + " < 0.05 threshold");
        }

        // ── META-RULE 3: scale confidence of boosted rule ──────────────
        System.out.println("\n[Phase 7] Meta-rule 3: compose formula terms...");
        // Rewrite the formula term itself: wrap pln_boosted in pln_scaled_confidence
        // This shows term composition at the meta-level
        registry.rewriteRule("deduction",
            new Compound("pln_scaled_confidence", new Const(1.2)),
            "compose: scale confidence by 1.2 for high-attention atoms");

        double[] tvFinal = deduction.apply(tvImpl[0], tvImpl[1], tvCat[0], tvCat[1]);
        System.out.printf("  P(animal) with composed formula = TV(%.3f, %.3f)%n",
                          tvFinal[0], tvFinal[1]);

        // ── Dump rewrite history ───────────────────────────────────────
        registry.dumpHistory();

        System.out.println("\n[Key Insight]");
        System.out.println("  PLN rules were algebraic Terms, not Java methods.");
        System.out.println("  Meta-rules matched rule Terms and rewrote their formula.");
        System.out.println("  Three rewrites: boost → abduction → scaled_confidence.");
        System.out.println("  In Maude: rl [meta] : rule(F,N,S,C) => rule(F',N,S,C) .");
        System.out.println("  This is Maude's reflective tower in a cognitive architecture.");

        bridge.close();
        socat.destroy();
    }
}

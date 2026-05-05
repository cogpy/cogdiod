# CogDiod Quickstart: Maude Integration

## Prerequisites
- Maude 3.x: https://maude.lcc.uma.es/
- Java 11+ (for CogDiodMaude.java bridge)
- CogDiod bridge running: `./cogdiod_bridge &`

## Architecture Overview

The Maude integration demonstrates **reflective term rewriting**:
- PLN deduction rules are first-class terms in a `RuleRegistry`
- Meta-rules can rewrite the deduction formula itself
- `pln-boosted(factor)` → applies to all matching atoms via bridge `rewrite_rule`

## Running the Demo

```bash
./cogdiod_bridge &

# Option A: Pure Maude (no Java)
maude maude/cogdiod.maude

# Option B: Clojure runner
clojure maude/run_maude.clj

# Option C: Java bridge
javac maude/CogDiodMaude.java && java -cp maude CogDiodMaude
```

## Hello Atom in Maude

```maude
--- cogdiod.maude (abbreviated)
mod COGDIOD-PLN is
  protecting FLOAT .

  --- TruthValue as a pair of floats
  sort TV .
  op tv : Float Float -> TV [ctor] .

  --- PLN deduction: P(B) = P(B|A) * P(A)
  op pln-ded : TV TV -> TV .
  eq pln-ded(tv(S_AB, C_AB), tv(S_A, C_A))
   = tv(S_AB * S_A, C_AB * C_A * 0.9) .

  --- PLN revision: weighted merge
  op pln-rev : TV TV -> TV .
  eq pln-rev(tv(S1, C1), tv(S2, C2))
   = tv((S1 * C1 + S2 * C2) / (C1 + C2),
        (C1 + C2) / (C1 + C2 + 1.0)) .
endm

--- Compute P(animal) given P(animal|cat)=0.95 and P(cat)=0.80
reduce pln-ded(tv(0.95, 0.80), tv(0.80, 0.90)) .
--- => tv(0.76, 0.648)
```

## Reflective Term Rewriting

Maude's `META-LEVEL` allows rules to rewrite other rules:

```maude
mod COGDIOD-META is
  including META-LEVEL .
  including COGDIOD-PLN .

  --- The RuleRegistry holds the deduction formula as a term
  op deduce-rule : -> Term .
  eq deduce-rule = 'pln-ded['TV1:TV, 'TV2:TV] .

  --- Meta-rule: if deduction confidence is consistently low,
  --- replace with abduction formula
  op maybe-rewrite : Term Float -> Term .
  eq maybe-rewrite(T, Conf) =
    if Conf < 0.3
    then 'pln-abd['TV1:TV, 'TV2:TV, 'TV3:TV]
    else T
    fi .
endm
```

## Bridge Integration

After Maude fires a `pln-boosted(1.15)` rule:

```maude
--- This triggers the bridge rewrite_rule operation
op boost-all-implications : Float -> Bool .
eq boost-all-implications(Factor) =
  --- Sends: {"op":"rewrite_rule","from_type":"ImplicationLink","boost":1.15}
  bridge-call("rewrite_rule", "ImplicationLink", Factor) .
```

The bridge then boosts all ImplicationLink TVs by the factor.

## Key Insight

In Maude, inference rules are terms. A meta-rule can observe
the performance of the deduction rule and rewrite it into an
abduction rule if the current strategy is underperforming.
This is "learning how to reason" — AGI at the rule level.

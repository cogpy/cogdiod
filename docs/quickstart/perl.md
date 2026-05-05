# CogDiod Quickstart: Perl Integration

## Prerequisites
- Perl 5.20+: usually pre-installed
- `JSON::PP` module: included in Perl core since 5.14
- CogDiod bridge running: `./cogdiod_bridge &`

## Architecture Overview

The Perl integration demonstrates:
- **Context-sensitive evaluation** via `use overload`
  - `"$atom"` — string context returns `Type('name') TV=(s,c) STI=n`
  - `$atom + 0` — numeric context returns STI
  - `@{$atom}` — array context returns linked atom UUIDs
  - `$atom ? ... : ...` — boolean context returns `strength > 0.5`
- **AUTOLOAD** — any undefined method `ecan_*` or `pln_*` is auto-generated
- **inject_capability** — add closures to live atom instances at runtime

## Running the Demo

```bash
./cogdiod_bridge &
perl perl/cogdiod.pl
```

## Hello Atom

```perl
use strict; use warnings;
# (After loading CogDiodBridge and CogAtom from cogdiod.pl)

my $bridge = CogDiodBridge->new();
my $uuid = $bridge->spawn("ConceptNode", "cat", 0.80, 0.90);

my $cat = CogAtom->new(
    uuid => $uuid, type => "ConceptNode", name => "cat",
    strength => 0.80, confidence => 0.90, sti => 0.72,
    bridge => $bridge,
);

# String context
printf "String: %s\n", $cat;  # => ConceptNode('cat') TV=(0.80,0.90) STI=0.72

# Numeric context (STI)
printf "Numeric: %.2f\n", $cat + 0;  # => 0.72

# Boolean context
printf "Bool: %s\n", $cat ? "believed" : "doubted";  # => believed
```

## AUTOLOAD in Action

```perl
# pln_revise is not defined — AUTOLOAD generates it
$cat->pln_revise(0.85, 0.70);
# => [pln_revise] cat: TV(0.800,0.900) + TV(0.850,0.700) => TV(0.823,0.762)

# ecan_spread is not defined — AUTOLOAD generates it
$cat->ecan_spread(2.5);
# => [ecan_spread] cat STI: 0.72 -> 3.22
```

## Runtime Capability Injection

```perl
# Inject a custom PLN deduction capability
$animal->inject_capability('deduce_from', sub {
    my ($self, $impl_atom, $ant_atom) = @_;
    my $s = $impl_atom->{strength} * $ant_atom->{strength};
    my $c = $impl_atom->{confidence} * $ant_atom->{confidence} * 0.9;
    $self->{bridge}->set_tv($self->{uuid}, $s, $c);
    return ($s, $c);
});

$animal->deduce_from($impl, $cat);
# => [deduce_from] P(animal|cat) = TV(0.760, 0.648)
```

## CogDiod::Agent Base Class

The `CogDiod::Agent` base class (at the top of `cogdiod.pl`) provides:
- Automatic bridge connection on `new()`
- PLN deduction, revision, abduction via `AUTOLOAD`
- ECAN spread and decay via `AUTOLOAD`
- `introspect()` — dump full atom state

```perl
my $agent = CogDiod::Agent->new(
    type => "ConceptNode", name => "my_concept",
    strength => 0.7, confidence => 0.8,
);
$agent->pln_revise(0.9, 0.95);
$agent->ecan_spread(3.0);
$agent->introspect();
```

## Key Insight

In Perl, every object is open. `AUTOLOAD` means there is no closed
class — new cognitive capabilities can be injected into any atom
at any time. This is Perl's TMTOWTDI philosophy applied to cognition.

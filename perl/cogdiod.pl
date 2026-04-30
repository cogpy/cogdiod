#!/usr/bin/env perl
# cogdiod.pl — CogDiod Perl Integration
# =======================================
#
# Perl's superpower for CogDiod: context-sensitive evaluation + AUTOLOAD
#
# In Perl, the same expression returns different types depending on context:
#   my @list  = @atom;     # list context: returns linked neighbours
#   my $tv    = $atom;     # scalar context: returns truth value string
#   my %hash  = %atom;     # hash context: returns full atom properties
#
# AUTOLOAD intercepts any undefined method call on an Atom object.
# This means we can inject new cognitive capabilities (like ecan_spread,
# pln_deduce, moses_evolve) into live Atom instances WITHOUT modifying
# the Atom class — exactly like Perl's AUTOLOAD-based metaprogramming.
#
# The bridge communication uses IO::Socket::UNIX directly.

use strict;
use warnings;
use IO::Socket::UNIX;
use Socket qw(AF_UNIX SOCK_STREAM);
use JSON::PP;   # core module, no install needed
use POSIX qw(floor);

# ── Bridge client ──────────────────────────────────────────────────────────

package CogDiodBridge;

sub new {
    my ($class) = @_;
    my $sock = IO::Socket::UNIX->new(
        Type => Socket::SOCK_STREAM,
        Peer => '/tmp/cogdiod.sock',
    ) or die "Cannot connect to CogDiod bridge: $!";
    return bless { sock => $sock }, $class;
}

sub call {
    my ($self, $json) = @_;
    my $sock = $self->{sock};
    print $sock "$json\n";
    my $resp = <$sock>;
    chomp $resp;
    return JSON::PP::decode_json($resp);
}

sub spawn {
    my ($self, $type, $name, $s, $c) = @_;
    my $r = $self->call(sprintf(
        '{"op":"spawn","type":"%s","name":"%s","strength":%.4f,"confidence":%.4f}',
        $type, $name, $s, $c
    ));
    return $r->{uuid};
}

sub get_tv {
    my ($self, $uuid) = @_;
    return $self->call(sprintf('{"op":"get_tv","uuid":%d}', $uuid));
}

sub set_tv {
    my ($self, $uuid, $s, $c) = @_;
    $self->call(sprintf(
        '{"op":"set_tv","uuid":%d,"strength":%.4f,"confidence":%.4f}',
        $uuid, $s, $c
    ));
}

sub link {
    my ($self, $from, $to) = @_;
    $self->call(sprintf('{"op":"link","from":%d,"to":%d}', $from, $to));
}

sub get_stats {
    my ($self) = @_;
    return $self->call('{"op":"stats"}');
}

# ── Atom class with context-sensitive overloading ─────────────────────────
#
# Perl's 'use overload' allows different behaviour in different contexts.
# This is the Perl equivalent of Maude's sort polymorphism.

package CogAtom;
use overload
    '""'     => \&as_string,      # string context: "ConceptNode(cat) TV=(0.80,0.90)"
    '0+'     => \&as_number,      # numeric context: STI value
    '@{}'    => \&as_list,        # array deref context: list of linked UUIDs
    'bool'   => \&as_bool,        # boolean context: strength > 0.5
    fallback => 1;
# NOTE: %{} overload is intentionally omitted — overloading hash deref on a
# blessed hashref causes infinite recursion. Use ->as_hash() explicitly instead.

sub new {
    my ($class, %args) = @_;
    return bless {
        uuid       => $args{uuid},
        type       => $args{type},
        name       => $args{name},
        strength   => $args{strength}    // 0.0,
        confidence => $args{confidence}  // 0.0,
        sti        => $args{sti}         // 0.0,
        lti        => $args{lti}         // 0.0,
        links      => $args{links}       // [],
        bridge     => $args{bridge},
        _injected  => {},    # AUTOLOAD-injected capabilities
    }, $class;
}

# String context: human-readable representation
sub as_string {
    my ($self) = @_;
    return sprintf("%s('%s') TV=(%.2f,%.2f) STI=%.2f",
        $self->{type}, $self->{name},
        $self->{strength}, $self->{confidence},
        $self->{sti});
}

# Numeric context: return STI (short-term importance)
sub as_number {
    my ($self) = @_;
    return $self->{sti};
}

# Array dereference context: return linked atom UUIDs
sub as_list {
    my ($self) = @_;
    return $self->{links};
}

# Hash dereference context: return full properties as hash
# NOTE: must return a plain list (key => val pairs), NOT a hashref,
# to avoid infinite recursion when Perl dereferences the result.
sub as_hash {
    my ($self) = @_;
    return (
        uuid       => $self->{uuid},
        type       => $self->{type},
        name       => $self->{name},
        strength   => $self->{strength},
        confidence => $self->{confidence},
        sti        => $self->{sti},
        lti        => $self->{lti},
        links      => $self->{links},
    );
}

# Boolean context: is this atom "believed"? (strength > 0.5)
sub as_bool {
    my ($self) = @_;
    return $self->{strength} > 0.5 ? 1 : 0;
}

# ── AUTOLOAD: inject capabilities at runtime ──────────────────────────────
#
# Any method not defined on CogAtom is intercepted by AUTOLOAD.
# If a capability has been injected (via inject_capability), it is called.
# Otherwise, a default behaviour is generated dynamically.
#
# This is the Perl equivalent of Clojure's extend-protocol: adding
# behaviour to an existing type without modifying its definition.

our $AUTOLOAD;

sub AUTOLOAD {
    my ($self, @args) = @_;
    my $method = $AUTOLOAD;
    $method =~ s/.*:://;  # strip package name

    return if $method eq 'DESTROY';

    # Check if a capability was injected
    if (exists $self->{_injected}{$method}) {
        return $self->{_injected}{$method}->($self, @args);
    }

    # Auto-generate a default capability based on naming convention
    if ($method =~ /^ecan_(.+)$/) {
        my $op = $1;
        warn "  [AUTOLOAD] auto-generating ecan_$op for $self->{name}\n";
        return $self->_ecan_default($op, @args);
    }

    if ($method =~ /^pln_(.+)$/) {
        my $op = $1;
        warn "  [AUTOLOAD] auto-generating pln_$op for $self->{name}\n";
        return $self->_pln_default($op, @args);
    }

    die "Unknown method '$method' on CogAtom '$self->{name}'\n";
}

sub _ecan_default {
    my ($self, $op, @args) = @_;
    if ($op eq 'spread') {
        my $amount = $args[0] // 1.0;
        $self->{sti} += $amount;
        printf("  [ecan_spread] %s STI: %.2f -> %.2f\n",
               $self->{name}, $self->{sti} - $amount, $self->{sti});
        return $self->{sti};
    }
    return undef;
}

sub _pln_default {
    my ($self, $op, @args) = @_;
    if ($op eq 'revise') {
        my ($s2, $c2) = @args;
        # PLN revision formula
        my $k  = 1.0;
        my $s1 = $self->{strength};
        my $c1 = $self->{confidence};
        my $cn = $c1 + $c2;
        my $sn = ($cn > 0) ? ($s1 * $c1 + $s2 * $c2) / $cn : 0;
        my $cc = $cn / ($cn + $k);
        printf("  [pln_revise] %s: TV(%.3f,%.3f) + TV(%.3f,%.3f) => TV(%.3f,%.3f)\n",
               $self->{name}, $s1, $c1, $s2, $c2, $sn, $cc);
        $self->{strength}   = $sn;
        $self->{confidence} = $cc;
        $self->{bridge}->set_tv($self->{uuid}, $sn, $cc) if $self->{bridge};
        return ($sn, $cc);
    }
    return undef;
}

# Inject a capability closure into this atom instance
sub inject_capability {
    my ($self, $name, $code) = @_;
    $self->{_injected}{$name} = $code;
    printf("  [inject] '%s' capability added to %s\n", $name, $self->{name});
}

# Sync TV from bridge
sub sync_tv {
    my ($self) = @_;
    my $tv = $self->{bridge}->get_tv($self->{uuid});
    $self->{strength}   = $tv->{strength};
    $self->{confidence} = $tv->{confidence};
}

# ── Context-sensitive evaluation ──────────────────────────────────────────
#
# The same atom evaluated in different contexts returns different things.
# This is Perl's unique contribution: not just polymorphism, but
# context-sensitive semantics built into the language itself.

package CogContext;

use constant {
    LOGICAL     => 'logical',
    ASSOCIATIVE => 'associative',
    TEMPORAL    => 'temporal',
    SPATIAL     => 'spatial',
};

sub evaluate {
    my ($atom, $context, %opts) = @_;

    if ($context eq LOGICAL) {
        # Logical context: return TruthValue as a probability
        return { strength => $atom->{strength}, confidence => $atom->{confidence} };

    } elsif ($context eq ASSOCIATIVE) {
        # Associative context: return Hebbian-linked neighbours
        # (array dereference context on the atom)
        my @links = @{$atom};
        return \@links;

    } elsif ($context eq TEMPORAL) {
        # Temporal context: STI-discounted TruthValue
        my $decay  = $opts{decay} // 0.95;
        my $steps  = $opts{steps} // 1;
        my $factor = $decay ** $steps;
        return {
            strength   => $atom->{strength} * $factor,
            confidence => $atom->{confidence} * $factor,
            decay_factor => $factor,
        };

    } elsif ($context eq SPATIAL) {
        # Spatial context: return neighbourhood radius
        my $sti    = $atom + 0;  # numeric context: STI
        my $radius = sqrt($sti + 1.0);
        return { sti => $sti, radius => $radius };
    }

    die "Unknown context: $context\n";
}

# ── Main ──────────────────────────────────────────────────────────────────

package main;

print "=" x 60, "\n";
print "PERL INTEGRATION: Context-Sensitive Atoms + AUTOLOAD\n";
print "=" x 60, "\n";

my $bridge = CogDiodBridge->new();

# Phase 1: Spawn atoms
print "\n[Phase 1] Spawning atoms...\n";
my $cat_uuid    = $bridge->spawn("ConceptNode",    "cat",        0.80, 0.90);
my $animal_uuid = $bridge->spawn("ConceptNode",    "animal",     0.50, 0.60);
my $impl_uuid   = $bridge->spawn("ImplicationLink","cat->animal",0.95, 0.80);
$bridge->link($cat_uuid, $impl_uuid);
$bridge->link($impl_uuid, $animal_uuid);

printf("  spawned: cat=%d, animal=%d, impl=%d\n",
       $cat_uuid, $animal_uuid, $impl_uuid);

# Phase 2: Create Atom objects with context-sensitive overloading
print "\n[Phase 2] Creating context-sensitive Atom objects...\n";
my $cat = CogAtom->new(
    uuid => $cat_uuid, type => "ConceptNode", name => "cat",
    strength => 0.80, confidence => 0.90, sti => 0.72,
    links => [$impl_uuid], bridge => $bridge
);
my $animal = CogAtom->new(
    uuid => $animal_uuid, type => "ConceptNode", name => "animal",
    strength => 0.50, confidence => 0.60, sti => 0.30,
    links => [], bridge => $bridge
);

# Phase 3: Context-sensitive evaluation
print "\n[Phase 3] Context-sensitive evaluation of 'cat'...\n";

# String context
printf("  string context:  %s\n", $cat);

# Numeric context (STI)
printf("  numeric context: STI = %.2f\n", $cat + 0);

# Boolean context
printf("  boolean context: believed = %s\n", $cat ? "true" : "false");

# Hash context (full properties) — use ->as_hash() explicitly
# (Perl cannot overload %{} on a blessed hashref without infinite recursion)
my %props = $cat->as_hash();
printf("  hash context:    {type=%s, strength=%.2f, confidence=%.2f}\n",
       $props{type}, $props{strength}, $props{confidence});

# Array context (linked UUIDs)
my @linked = @{$cat};
printf("  array context:   linked UUIDs = [%s]\n", join(", ", @linked));

# Logical context
my $logical = CogContext::evaluate($cat, CogContext::LOGICAL);
printf("  LOGICAL ctx:     TV(%.3f, %.3f)\n",
       $logical->{strength}, $logical->{confidence});

# Temporal context (3 steps of decay)
my $temporal = CogContext::evaluate($cat, CogContext::TEMPORAL, steps => 3, decay => 0.95);
printf("  TEMPORAL ctx:    TV(%.3f, %.3f) [decay^3=%.3f]\n",
       $temporal->{strength}, $temporal->{confidence}, $temporal->{decay_factor});

# Spatial context
my $spatial = CogContext::evaluate($cat, CogContext::SPATIAL);
printf("  SPATIAL ctx:     STI=%.2f, radius=%.3f\n",
       $spatial->{sti}, $spatial->{radius});

# Phase 4: AUTOLOAD — auto-generate ecan_spread
print "\n[Phase 4] AUTOLOAD: calling undefined method ecan_spread...\n";
$cat->ecan_spread(2.5);
printf("  cat STI after spread: %.2f\n", $cat + 0);

# Phase 5: AUTOLOAD — auto-generate pln_revise
print "\n[Phase 5] AUTOLOAD: calling undefined method pln_revise...\n";
$cat->pln_revise(0.85, 0.70);

# Phase 6: inject_capability — add a custom method at runtime
print "\n[Phase 6] inject_capability: adding 'introspect' to cat at runtime...\n";
$cat->inject_capability('introspect', sub {
    my ($self) = @_;
    printf("  [introspect] I am '%s' (uuid=%d). My formula:\n", $self->{name}, $self->{uuid});
    printf("    TV = (%.3f, %.3f)\n", $self->{strength}, $self->{confidence});
    printf("    STI = %.2f, LTI = %.2f\n", $self->{sti}, $self->{lti});
    printf("    Injected capabilities: [%s]\n", join(", ", keys %{$self->{_injected}}));
    printf("    Linked atoms: [%s]\n", join(", ", @{$self->{links}}));
});

$cat->introspect();

# Phase 7: inject_capability — add PLN deduction to animal
print "\n[Phase 7] inject_capability: adding 'deduce_from' to animal...\n";
$animal->inject_capability('deduce_from', sub {
    my ($self, $impl_atom, $ant_atom) = @_;
    my $s = $impl_atom->{strength} * $ant_atom->{strength};
    my $c = $impl_atom->{confidence} * $ant_atom->{confidence} * 0.9;
    printf("  [deduce_from] P(animal|cat) = TV(%.3f, %.3f)\n", $s, $c);
    $self->{strength}   = $s;
    $self->{confidence} = $c;
    $self->{bridge}->set_tv($self->{uuid}, $s, $c);
    return ($s, $c);
});

my $impl_atom = CogAtom->new(
    uuid => $impl_uuid, type => "ImplicationLink", name => "cat->animal",
    strength => 0.95, confidence => 0.80, bridge => $bridge
);
$animal->deduce_from($impl_atom, $cat);

# Phase 8: Verify final state
print "\n[Phase 8] Final state via bridge...\n";
my $tv = $bridge->get_tv($animal_uuid);
printf("  animal TV = (%.3f, %.3f)\n", $tv->{strength}, $tv->{confidence});

print "\n[Key Insight]\n";
print "  The same \$cat atom returned 6 different types in 6 contexts.\n";
print "  ecan_spread and pln_revise were NEVER defined in CogAtom.\n";
print "  AUTOLOAD generated them dynamically from naming conventions.\n";
print "  introspect and deduce_from were injected as closures at runtime.\n";
print "  In Perl: there is no 'closed' class — every object is open.\n";
print "  This is the Perl philosophy: TIMTOWTDI applied to cognition.\n";

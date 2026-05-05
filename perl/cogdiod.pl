    die "Unknown context: $context\n";
}

# ── CogDiod::Agent — Base class for autonomous cognitive agents ───────────
#
# Phase 6.26: Provides a ready-to-use agent base class that automatically:
#   - Connects to the bridge on construction
#   - Spawns an atom in the AtomSpace
#   - Provides PLN and ECAN operations via AUTOLOAD naming conventions
#   - Tracks and propagates episodic TV history
#
# Usage:
#   package MyAgent;
#   our @ISA = ('CogDiod::Agent');
#
#   sub new {
#       my ($class, %args) = @_;
#       return $class->SUPER::new(type => "ConceptNode", name => "my_agent",
#                                  strength => 0.7, confidence => 0.8);
#   }
#
#   # Override on_message to define custom behaviour
#   sub on_message {
#       my ($self, $msg_type, $tv) = @_;
#       $self->pln_revise($tv->{strength}, $tv->{confidence});
#   }

package CogDiod::Agent;

sub new {
    my ($class, %args) = @_;
    my $bridge = CogDiodBridge->new();
    my $type   = $args{type}       // "ConceptNode";
    my $name   = $args{name}       // "agent_" . int(rand(10000));
    my $s      = $args{strength}   // 0.5;
    my $c      = $args{confidence} // 0.5;

    my $uuid = $bridge->spawn($type, $name, $s, $c);

    my $self = bless {
        uuid        => $uuid,
        type        => $type,
        name        => $name,
        strength    => $s,
        confidence  => $c,
        sti         => 0.0,
        lti         => 0.0,
        links       => [],
        bridge      => $bridge,
        _injected   => {},
        _history    => [],    # episodic TV history
    }, $class;

    return $self;
}

# Delegate AUTOLOAD to the CogAtom implementation (PLN/ECAN generation)
our $AUTOLOAD;

sub AUTOLOAD {
    my ($self, @args) = @_;
    my $method = $AUTOLOAD;
    $method =~ s/.*:://;
    return if $method eq 'DESTROY';

    # Redirect to CogAtom AUTOLOAD logic
    if (exists $self->{_injected}{$method}) {
        return $self->{_injected}{$method}->($self, @args);
    }

    if ($method =~ /^ecan_(.+)$/) {
        my $op = $1;
        warn "  [Agent::AUTOLOAD] auto-generating ecan_$op for $self->{name}\n";
        if ($op eq 'spread') {
            my $amount = $args[0] // 1.0;
            $self->{sti} += $amount;
            printf("  [ecan_spread] agent %s STI: %.2f -> %.2f\n",
                   $self->{name}, $self->{sti} - $amount, $self->{sti});
            $self->{bridge}->call(sprintf(
                '{"op":"attend","uuid":%d,"delta":%.4f}',
                $self->{uuid}, $amount));
            return $self->{sti};
        }
        if ($op eq 'decay') {
            my $factor = $args[0] // 0.95;
            $self->{sti} *= $factor;
            return $self->{sti};
        }
        return undef;
    }

    if ($method =~ /^pln_(.+)$/) {
        my $op = $1;
        warn "  [Agent::AUTOLOAD] auto-generating pln_$op for $self->{name}\n";
        if ($op eq 'revise') {
            my ($s2, $c2) = @args;
            push @{$self->{_history}}, { s => $self->{strength}, c => $self->{confidence} };
            my $k  = 1.0;
            my $s1 = $self->{strength};
            my $c1 = $self->{confidence};
            my $cn = $c1 + $c2;
            my $sn = ($cn > 0) ? ($s1 * $c1 + $s2 * $c2) / $cn : 0;
            my $cc = $cn / ($cn + $k);
            printf("  [pln_revise] agent %s: TV(%.3f,%.3f) + TV(%.3f,%.3f) => TV(%.3f,%.3f)\n",
                   $self->{name}, $s1, $c1, $s2, $c2, $sn, $cc);
            $self->{strength}   = $sn;
            $self->{confidence} = $cc;
            $self->{bridge}->set_tv($self->{uuid}, $sn, $cc);
            return ($sn, $cc);
        }
        if ($op eq 'deduce') {
            my ($impl_s, $impl_c, $ant_s, $ant_c) = @args;
            my $s_out = $impl_s * $ant_s;
            my $c_out = $impl_c * $ant_c * 0.9;
            printf("  [pln_deduce] agent %s: P(B) = TV(%.3f,%.3f)\n",
                   $self->{name}, $s_out, $c_out);
            $self->{strength}   = $s_out;
            $self->{confidence} = $c_out;
            $self->{bridge}->set_tv($self->{uuid}, $s_out, $c_out);
            return ($s_out, $c_out);
        }
        return undef;
    }

    die "Unknown method '$method' on CogDiod::Agent '$self->{name}'\n";
}

sub inject_capability {
    my ($self, $name, $code) = @_;
    $self->{_injected}{$name} = $code;
    printf("  [inject] '%s' added to agent %s\n", $name, $self->{name});
}

sub introspect {
    my ($self) = @_;
    printf("  [introspect] Agent '%s' (uuid=%d, type=%s)\n",
           $self->{name}, $self->{uuid}, $self->{type});
    printf("    TV=(%.3f, %.3f)  STI=%.2f\n",
           $self->{strength}, $self->{confidence}, $self->{sti});
    printf("    History: %d versions\n", scalar @{$self->{_history}});
    printf("    Injected: [%s]\n", join(", ", keys %{$self->{_injected}}));
}

sub uuid   { return $_[0]->{uuid}   }
sub bridge { return $_[0]->{bridge} }

# ── Main ──────────────────────────────────────────────────────────────────

package main;

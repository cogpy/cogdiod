#!/usr/bin/env bash
# run_all.sh — CogDiod Native Language Integration Runner
# =========================================================
#
# Runs all five language integrations against the live CogDiod bridge.
#
# Language → Runtime mapping:
#   Maude  → cogdiod.maude  (canonical source) + run_maude.clj (Clojure executor)
#   Perl   → cogdiod.pl     (native Perl 5.34, IO::Socket::UNIX)
#   Racket → cogdiod.rkt    (canonical source) + cogdiod_hy.hy (Hy executor)
#   Clojure→ cogdiod.clj    (native Clojure 1.12 / Java 11)
#   Guile  → cogdiod.scm    (canonical source) + cogdiod_hy.hy (Hy executor)

set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BRIDGE="$ROOT/../cogdiod-lang/bridge/cogdiod_bridge"

# ── Build bridge if needed ────────────────────────────────────────────────
if [ ! -f "$BRIDGE" ]; then
  echo "Building C bridge server..."
  cd "$ROOT/../cogdiod-lang/bridge"
  zig cc -O2 -std=c11 -o cogdiod_bridge cogdiod_bridge.c -lpthread -lm
  cd "$ROOT"
fi

# ── Start bridge server ───────────────────────────────────────────────────
echo "================================================================="
echo "STARTING C BRIDGE SERVER"
echo "================================================================="
killall cogdiod_bridge 2>/dev/null || true
rm -f /tmp/cogdiod.sock
"$BRIDGE" &
BRIDGE_PID=$!
sleep 0.5
echo "[bridge] PID=$BRIDGE_PID socket=/tmp/cogdiod.sock"

# ── Start socat TCP bridge for Clojure (Java 11 has no AF_UNIX) ──────────
socat TCP-LISTEN:19999,reuseaddr,fork UNIX-CONNECT:/tmp/cogdiod.sock &
SOCAT_PID=$!
sleep 0.3
echo "[socat] PID=$SOCAT_PID TCP:19999 → UNIX:/tmp/cogdiod.sock"

cleanup() {
  kill $BRIDGE_PID $SOCAT_PID 2>/dev/null || true
  rm -f /tmp/cogdiod.sock
}
trap cleanup EXIT

# ── 1. MAUDE ──────────────────────────────────────────────────────────────
echo ""
echo "================================================================="
echo "RUNNING: Maude (cogdiod.maude + Clojure executor)"
echo "================================================================="
echo "  [canonical source]  $ROOT/maude/cogdiod.maude"
echo "  [executor]          clojure run_maude.clj"
echo ""
cat "$ROOT/maude/run_maude.clj" | clojure -M - 2>&1
echo ""
echo "MAUDE: DONE"

# ── 2. PERL ───────────────────────────────────────────────────────────────
echo ""
echo "================================================================="
echo "RUNNING: Perl (cogdiod.pl — native Perl 5.34)"
echo "================================================================="
perl "$ROOT/perl/cogdiod.pl" 2>&1
echo ""
echo "PERL: DONE"

# ── 3. RACKET ─────────────────────────────────────────────────────────────
echo ""
echo "================================================================="
echo "RUNNING: Racket (cogdiod.rkt + Hy executor)"
echo "================================================================="
echo "  [canonical source]  $ROOT/racket/cogdiod.rkt"
echo "  [executor]          hy cogdiod_hy.hy"
echo ""
hy "$ROOT/racket/cogdiod_hy.hy" 2>&1
echo ""
echo "RACKET: DONE"

# ── 4. CLOJURE ────────────────────────────────────────────────────────────
echo ""
echo "================================================================="
echo "RUNNING: Clojure (cogdiod.clj — native Clojure 1.12)"
echo "================================================================="
cat "$ROOT/clojure/cogdiod.clj" | clojure -M - 2>&1
echo ""
echo "CLOJURE: DONE"

# ── 5. GUILE ──────────────────────────────────────────────────────────────
echo ""
echo "================================================================="
echo "RUNNING: Guile Scheme (cogdiod.scm + Hy executor)"
echo "================================================================="
echo "  [canonical source]  $ROOT/guile/cogdiod.scm"
echo "  [executor]          hy cogdiod_hy.hy"
echo ""
hy "$ROOT/guile/cogdiod_hy.hy" 2>&1
echo ""
echo "GUILE: DONE"

echo ""
echo "================================================================="
echo "ALL FIVE LANGUAGE INTEGRATIONS COMPLETE"
echo "================================================================="
echo ""
echo "Native source files:"
echo "  Maude:   $ROOT/maude/cogdiod.maude"
echo "  Perl:    $ROOT/perl/cogdiod.pl"
echo "  Racket:  $ROOT/racket/cogdiod.rkt"
echo "  Clojure: $ROOT/clojure/cogdiod.clj"
echo "  Guile:   $ROOT/guile/cogdiod.scm"

/*
 * cogdiod_bridge.h — Shared C bridge for all language integrations
 *
 * This header defines the minimal AtomSpace API that each language module
 * (Maude, Perl, Racket, Clojure, Guile) binds to.  It is deliberately
 * simple: a flat C struct-based AtomSpace with JSON serialisation so that
 * every language can read/write atoms without a native C extension.
 *
 * The bridge exposes a UNIX-domain socket at /tmp/cogdiod.sock that speaks
 * a newline-delimited JSON protocol:
 *
 *   Request:  {"op":"get_tv","uuid":1}
 *   Response: {"strength":0.8,"confidence":0.9}
 *
 *   Request:  {"op":"set_tv","uuid":1,"strength":0.8,"confidence":0.9}
 *   Response: {"ok":true}
 *
 *   Request:  {"op":"spawn","type":"ConceptNode","name":"cat"}
 *   Response: {"uuid":1}
 *
 *   Request:  {"op":"link","from":1,"to":3}
 *   Response: {"ok":true}
 *
 *   Request:  {"op":"get_links","uuid":1,"dir":"out"}
 *   Response: {"uuids":[3]}
 *
 *   Request:  {"op":"get_sti","uuid":1}
 *   Response: {"sti":5.0}
 *
 *   Request:  {"op":"attend","uuid":1,"delta":2.0}
 *   Response: {"ok":true}
 *
 *   Request:  {"op":"snapshot"}
 *   Response: {"atoms":[{"uuid":1,"type":"ConceptNode","name":"cat",
 *                        "strength":0.8,"confidence":0.9,"sti":5.0},
 *                       ...]}
 *
 *   Request:  {"op":"rewrite_rule","from_pattern":"...","to_pattern":"..."}
 *   Response: {"ok":true,"applied":3}
 *
 *   Request:  {"op":"stats"}
 *   Response: {"atom_count":3,"total_sti":5.0}
 */

#pragma once

#define BRIDGE_SOCK_PATH "/tmp/cogdiod.sock"
#define BRIDGE_MAX_ATOMS 4096
#define BRIDGE_BUF_SIZE  65536

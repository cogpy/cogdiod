/*
 * elbo_compiler.h — Elbo S-expr → Dis VM bytecode compiler
 */

#pragma once

#include "cogdiod.h"

/*
 * Compile an Elbo source string into an ElmPackage.
 *
 * source     — Elbo S-expression source text
 * type_name  — override the type name (NULL = use from (elbo-module ...))
 *
 * Returns a heap-allocated ElmPackage on success, NULL on parse error.
 * The caller owns the package and must free it via cogdiod_unload_package
 * or elm_package_free().
 *
 * Supported top-level forms:
 *   (elbo-module TypeName body...)
 *   (defun init (self) body...)
 *   (defun on-message (self msg) body...)
 *   (defun on-gc (self) body...)
 *
 * Supported body expressions:
 *   nop, halt
 *   (get-tv), (set-tv), (get-sti), (set-sti)
 *   (pln-ded), (pln-rev), (pln-abd), (pln-ind)
 *   (ecan-sp)
 *   (send chan-idx), (recv), (spawn type-idx)
 */
ElmPackage* elbo_compile(const char* source, const char* type_name);

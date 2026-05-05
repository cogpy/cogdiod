/*
 * elbo_compiler.h — Elbo (S-expression) → Dis VM bytecode compiler API
 */
#pragma once
#include "cogdiod.h"

/*
 * elbo_compile — compile an Elbo source string into an ElmPackage.
 *
 * The returned package has its bytecode allocated on the heap.
 * Caller owns the package; free with free(pkg->dis_bytecode) + free(pkg).
 * Returns NULL on allocation failure.
 */
ElmPackage* elbo_compile(const char* source, const char* type_name);

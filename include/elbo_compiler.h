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

/*
 * elbo_compile_file — compile an Elbo source file to an .elm package file.
 * (Phase 4.6)
 *
 * Reads source from src_path, compiles it with elbo_compile(), and writes
 * the resulting package to out_path using elm_save().
 *
 * Returns 0 on success, -1 on failure.
 */
int elbo_compile_file(const char* src_path, const char* out_path);

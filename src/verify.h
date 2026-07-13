// 2026-07-13 — C² derivation verification module.
//   Verifies that function bodies satisfy their derivation examples
//   by generating a test harness, compiling, and running against
//   the transpiled C code.

#ifndef C2_VERIFY_H
#define C2_VERIFY_H

#include <stddef.h>
#include "error.h"

/// Verify all derivation examples in the source.
/// `source` is the raw C² source text. `filename` is used for diagnostics.
/// If `print_output` is non-zero, print per-example PASS/FAIL.
/// Returns 0 if all pass, 1 if any fail, -1 on error.
int verify_source(const char* source, size_t source_len,
                  const char* filename, ErrorList* errors,
                  int print_output);

#endif // C2_VERIFY_H

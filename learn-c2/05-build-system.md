# 05 — Build System

## Commands

The `c2c` compiler has four commands:

```
c2c build <file> [-o output] [--emit-c]    # Transpile + compile (default)
c2c check <file>                             # Parse + typecheck + verify
c2c verify <file>                            # Run derivation tests
c2c derive <file>                            # Synthesize from examples
```

### `c2c build`

Full pipeline: parse → typecheck → VRP → borrow check → drop inject →
codegen → GCC/Clang → executable.

```bash
# Build to binary (calls gcc automatically)
./build/c2c build examples/swap_bytes.c2 -o swap_bytes

# Transpile to C only
./build/c2c build examples/swap_bytes.c2 --emit-c
# Output: _c2_out/swap_bytes.c
```

Exit codes:
| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (usage, verification failure) |
| 2 | Parse or codegen error |
| 3 | Type check error |
| 6 | System compiler (GCC) invocation failed |

### `c2c check`

Runs all analysis passes without generating code:

```bash
./build/c2c check examples/test_minimal.c2
```

Output includes Z3 contract verification (non-blocking):

```
Contract verification:
  abs_value: OK
  1/1 contracts verified successfully

c2: check passed for 'examples/test_minimal.c2'
```

### `c2c verify`

For functions that have both a body AND a derivation block, generates a
test harness, compiles it, runs it, and reports PASS/FAIL per example:

```bash
./build/c2c verify examples/swap_bytes.c2
```

This is also run automatically as a **build gate** during `c2c build` —
if any derivation example fails, the build stops.

### `c2c derive`

For functions that have a derivation block but NO body, runs the synthesis
engine to generate an implementation:

```bash
./build/c2c derive approx_sqrt.c2
```

Displays the Pareto frontier and inserts the selected expression into
the source file.

## Pipeline Overview

```
Source (.c2)
  │
  ├── 1. Lexer       ─── Token stream
  ├── 2. Parser      ─── AST (raw)
  ├── 3. Typecheck   ─── AST (types annotated)
  ├── 4. VRP         ─── AST (value ranges)
  ├── 5. Borrow      ─── AST (states validated)
  ├── 6. Drop Inject ─── AST (drop calls inserted)
  ├── 7. Codegen     ─── C source
  └── 8. Z3 Verify   ─── Proof results (advisory)
```

Passes 1–7 are mandatory for `build`. Pass 8 is non-blocking (reports
contract violations but doesn't stop compilation).

## Example Output

### Successful build
```
$ c2c build hello.c2 -o hello
c2: built 'hello.c2' -> 'hello'
```

### Contract violation
```
$ c2c check broken.c2

Contract verification:
  divide: FAIL (postcondition may be violated)
  0/1 contracts verified successfully

c2: check passed for 'broken.c2'
```

Note: contract verification is **non-blocking** during `build` and `check`.
The emitted C code is still produced.

### Derivation failure
```
$ c2c build regression.c2
error: derivation verification failed for 'regression.c2'
  Re-run `c2c verify regression.c2` for full details.
```

Derivation failures ARE build gates — the binary is kept but the build
exits with code 1.

## The `--emit-c` Flag

When you just want the generated C (for inspection or cross-compilation):

```bash
c2c build file.c2 --emit-c
# Writes to _c2_out/file.c
```

The emitted C includes:
- `#include <stdint.h>` for fixed-width types
- `restrict` on borrow/own pointer parameters
- `const` on borrow pointee data
- `if (...) { ... }` for `when` guards
- Derivation examples as `//` comments

## The `_c2_out/` Directory

Transpiled C files, test harnesses, and verification binaries go here:

```
_c2_out/
├── file.c                   # Emitted C source
├── verify_file.c            # Transpiled C (skip_main=1)
├── verify_harness_file.c    # Test harness
└── verify_bin_file          # Compiled verification binary
```

This directory is created automatically and should be in `.gitignore`.

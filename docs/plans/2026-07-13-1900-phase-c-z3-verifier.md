# Phase C: Z3 Contract Verifier

**DateTime:** 2026-07-13 1900
**Scope:** Implement compile-time contract verification using Z3 SMT solver.

## Architecture

Add a new pass `src/verifier.c` that walks the typed AST, translates contract
expressions into Z3 bit-vector formulas, and runs proof queries.

### Pipeline integration
- Add `z3_verify()` call after typecheck in `build` command
- Optional — if Z3 is missing, skip verification (not a hard error)
- Report PASS/FAIL per contract, summary at end

### Expression Translation (AST → Z3 BV)

| C² Expression | Z3 API |
|--------------|--------|
| `int literal` | `Z3_mk_bv_value(ctx, sort, val)` |
| `param reference` | `Z3_mk_const(ctx, sym, sort)` |
| `result` | `Z3_mk_const(ctx, "result", sort)` |
| `a + b` | `Z3_mk_bvadd(ctx, a, b)` |
| `a - b` | `Z3_mk_bvsub(ctx, a, b)` |
| `a * b` | `Z3_mk_bvmul(ctx, a, b)` |
| `a / b` | `Z3_mk_bvsdiv(ctx, a, b)` (signed) |
| `a == b` | `Z3_mk_eq(ctx, a, b)` → zero-extend to bv1 |
| `a != b` | `Z3_mk_not(ctx, Z3_mk_eq(...))` → zero-extend |
| `a < b` (signed) | `Z3_mk_bvslt(ctx, a, b)` → zero-extend |
| `a > b` | `Z3_mk_bvsgt(ctx, a, b)` |
| `a <= b` | `Z3_mk_bvsle(ctx, a, b)` |
| `a >= b` | `Z3_mk_bvsge(ctx, a, b)` |
| `!a` | `Z3_mk_not(ctx, a)` |
| `a && b` | `Z3_mk_and(ctx, a, b)` |
| `a \|\| b` | `Z3_mk_or(ctx, a, b)` |
| `~a` | `Z3_mk_bvnot(ctx, a)` |
| `a & b` | `Z3_mk_bvand(ctx, a, b)` |
| `a \| b` | `Z3_mk_bvor(ctx, a, b)` |
| `a ^ b` | `Z3_mk_bvxor(ctx, a, b)` |
| `a << b` | `Z3_mk_bvshl(ctx, a, b)` |
| `a >> b` | `Z3_mk_bvashr(ctx, a, b)` (arithmetic) |

### Verification Strategy

For each function with contracts:
1. Create Z3 context and bit-vector sorts (8/16/32/64 bit)
2. Declare symbolic constants for each parameter
3. If postcondition exists, declare symbolic constant for `result`
4. Translate pre and post expressions → Z3 formulas
5. **Precondition check:** Assert pre → check SAT (must be satisfiable — not trivially false)
6. **Postcondition check:** Assert pre, negate post → check UNSAT (post always holds when pre holds)
7. Destroy Z3 context
8. Report results

### Reliability
- If any translation step fails (unrecognized node), skip that function gracefully
- If Z3 library is not linked, skip all verification
- Errors go to stderr, not ErrorList (verification is advisory, not a hard compiler error)

## Files Touched
- `src/verifier.c` — NEW: Z3 contract verifier implementation
- `src/verifier.h` — NEW: Header for verifier API
- `src/main.c` — Wire z3_verify into build command
- `src/codegen.c` — Minor: export skip_main flag usage pattern
- `Makefile` — Use pkg-config Z3 flags for compilation
- `tests/verifier/test_verifier.c` — NEW: verifier tests

## Verification
- All existing tests still pass
- New verifier tests demonstrate correct PASS/FAIL detection
- `examples/test_minimal.c2` verifies without error
- `examples/swap_bytes.c2` verifies without error

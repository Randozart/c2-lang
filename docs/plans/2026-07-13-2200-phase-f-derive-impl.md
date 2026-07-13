# Phase F: Program Synthesis Engine — Implementation

**DateTime:** 2026-07-13 2200
**Scope:** Implement cost-guided enumerative search for derivation blocks.
           Handles hard/soft constraints, Pareto frontier, source insertion.
           See `docs/plans/2026-07-13-1800-phase-f-derive-engine.md` for full design.

## Architecture

### Expression Tree (`derive_expr` in derive.c)
Internal tree type for synthesized expressions:
- `EXPR_CONST` (int64/float64 literal)
- `EXPR_VAR` (parameter reference)
- `EXPR_UNARY` (op, child)
- `EXPR_BINARY` (op, left, right)

### Enumeration (breadth-first by op count)
1. Generate all trees with 1 op: `x`, `0`, `1`, `~x`, `-x`
2. Generate all trees with N ops by combining subtrees
3. Prune: `x + x` → `2*x`, `x * 1` → `x`, commutative dedup
4. Try constant bank values for leaf constants

### Evaluation
Tree-walking interpreter on concrete example inputs.
- Integer: 64-bit signed wrapping arithmetic
- Float: 64-bit IEEE 754

### Cost Model
- Hard examples: tolerance=0 → exact match required
- Soft examples: tolerance>0 → penalty = max(0, |actual - expected| - tol) / tol
- Total cost = op_count + sum(penalties)

### Pareto Frontier
- Track (op_count, penalty) pairs where no candidate dominates another
- Knee = closest to origin in (ops, penalty) space

### Source Mutation
- After derive, insert `return EXPR;\n}` at the function body position

## Files
- `src/derive.h` — NEW
- `src/derive.c` — NEW (expression enum, evaluation, cost, frontier, mutation)
- `src/main.c` — Wire `c2c derive` command
- `tests/derive/test_derive.c` — NEW

## Verification
- All existing tests pass
- Derive a trivial identity function from examples
- Derive `x + 1` from `{0->1; 1->2; 2->3;}`
- Pareto frontier correctly identifies trade-offs
PLANEOF
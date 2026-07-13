# Phase E: Borrow Checker + Drop Injection

**DateTime:** 2026-07-13 2100
**Scope:** Implement the lexical borrow checker and automatic drop injection.

## Design

### Borrow Checker (`src/borrow.c`)
Walks the typed AST and validates variable state transitions using the
5-state machine (`state.h`/`state.c`). For each variable access:
- **Read** (`x`, `x.field`, `*x`): requires OWNED or BORROWED state
- **Write** (`x = ...`, `*x = ...`): requires OWNED state
- **Move** (`own x = y`): transfers ownership (OWNED→MOVED for src, UNINIT→OWNED for dst)
- **Borrow** (`borrow x = &y`): creates a shared reference (OWNED→BORROWED)
- **Drop** (`free(x)`): early deallocation (OWNED→DROPPED)
- **Borrow end**: when borrow goes out of scope, BORROWED→OWNED

State transitions are validated via `state_transition()`.

### Drop Injection (`src/drop.c`)
Walks the AST and inserts `NODE_DROP_CALL` nodes for variables in OWNED
state that are about to go out of scope (at block/function boundaries).
- Skips variables already in STATE_DROPPED (supports early `free()`)
- Drop call is added as the last child of the block before the closing `}`

### Files
- `src/borrow.c` + `src/borrow.h` — NEW: borrow checker
- `src/drop.c` + `src/drop.h` — NEW: drop injection pass
- `src/main.c` — Wire borrow checker + drop injection after VRP
- `tests/borrow/test_borrow.c` — NEW: borrow checker tests
- `tests/drop/test_drop.c` — NEW: drop injection tests

## Verification
- All existing tests pass
- Borrow tests: read-after-move rejected, mutate-while-borrowed rejected
- Drop tests: owned variable gets drop call, early-free suppresses drop

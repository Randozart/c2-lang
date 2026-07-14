# Fix: Derivation `:= { ... } { body }` Syntax Support

**Date:** 2026-07-14
**Scope:** One-line parser change to support the derivation-then-body form.
**Files touched:** `src/parser.c`
**Architecture impact:** None — the AST already supports `body` alongside `deriv`.
**Verification:** `test_tc_derivation` in `tests/typecheck/test_typecheck.c:288-298`

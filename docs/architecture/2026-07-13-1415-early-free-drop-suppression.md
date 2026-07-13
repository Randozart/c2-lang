# Early `free()` Detection and Drop Suppression

**Date:** 2026-07-13  
**Status:** Design Decision (Phase E — Drop Injection)  
**Scope:** Borrow checker and drop injection pass interaction

---

## 1. Problem

In C², owned memory is automatically freed at scope boundaries via the drop injection pass. However, experienced C programmers often free memory *earlier* than the end of scope for performance reasons (reducing peak memory, timely release of locks, etc.). The compiler must support this pattern without introducing double-frees.

## 2. Design

### 2.1 Core Principle

A manually written `free(ptr)` call transitions the owning variable's state to `STATE_DROPPED`. The drop injection pass then skips variables already in the DROPPED state.

```
User writes free(ptr)
  → Borrow checker identifies owning variable of ptr
  → Transitions variable OWNED → DROPPED
  → Drop pass: variable already DROPPED → no injection
```

### 2.2 Ownership Tracking for `free()`

The borrow checker recognizes calls to `free()` as a special expression node (`NODE_FREE` emitted by the parser). It resolves `ptr` to its owning variable:

- **Direct pointer:** `free(vec)` — `vec` is found in the symbol table, transitioned to DROPPED.
- **Member access:** `free(vec->data)` — the checker resolves the base `vec` and flags it for *partial drop* tracking (see §3).

### 2.3 Drop Injection Logic Change

The current algorithm (spec §5.3):

```
At scope exit:
  For each symbol with state == OWNED:
    If type has [[c2::drop(fn)]]:
      Inject fn(&var)
```

Becomes:

```
At scope exit:
  For each symbol with state == OWNED:
    If type has [[c2::drop(fn)]]:
      Inject fn(&var)
  For each symbol with state == DROPPED:
    Do nothing (already freed by user)
```

### 2.4 Double-Free Detection

If the user calls `free(ptr)` twice on the same pointer, the second call finds the variable already in `STATE_DROPPED` and produces a compile-time error:

```
c2 error: double-free of variable 'vec'
  └─ src/main.c2:14:5
  note: first free at src/main.c2:10:5
```

### 2.5 Use-After-Free Detection

After `free(vec)`, any read or write through `vec` is rejected:

```
c2 error: use of freed variable 'vec'
  └─ src/main.c2:15:3
  note: freed at src/main.c2:10:5
```

## 3. Struct Field Tracking (v1 Approach)

For expressions like `free(vec->data)` where `vec` has `[[c2::drop(free_vector)]]`:

### 3.1 v1 Conservative Rule

The compiler does NOT attempt to remove individual field frees from the synthesized drop. Instead:

1. `free(vec->data)` is allowed as a user action.
2. The drop function `free_vector(&vec)` is still emitted at scope exit.
3. The drop function MUST be written to check for NULL before freeing:

```c
void free_vector(Vector* vec) {
    if (vec->data) {          // ← null check required
        free(vec->data);
        vec->data = NULL;
    }
}
```

### 3.2 User's Responsibility After Early Field Free

After calling `free(vec->data)` early, the user should set the field to NULL:

```c
free(vec->data);
vec->data = NULL;  // ← required to prevent double-free in drop
```

The compiler MAY warn if a field that was freed is not set to NULL before scope exit (future enhancement).

### 3.3 v2 Future Enhancement

A future version could track which pointer fields have been freed and elide those specific `free()` calls from the synthesized drop, removing the null-check requirement. This would require field-level state tracking in the symbol table.

## 4. Parser Changes

The parser emits `free(ptr)` calls as a new AST node kind:

```c
NODE_FREE  // free(expr) — transitions state to DROPPED
```

This is crafted by the parser when it sees a call to the identifier `free` with exactly one argument. This is functionally equivalent to `NODE_CALL` but carries semantic significance for the borrow checker.

Alternative: The borrow checker could pattern-match on `NODE_CALL` to identifier `free`. This avoids the parser change but is fragile. The explicit `NODE_FREE` node is preferred for reliability.

## 5. Parser Recognition

In `parse_postfix`, when a function call is detected:

```c
if (t.kind == TOK_IDENTIFIER && strcmp(t.text, "free") == 0
    && peek(p).kind == TOK_LPAREN) {
    // Emit NODE_FREE instead of NODE_CALL
}
```

This is done at parse time rather than later to keep the AST explicit about the semantics.

## 6. State Machine Update

Updated transition table (§5.2):

| Action | Current State → New State | Condition |
|--------|--------------------------|-----------|
| ... | ... (existing transitions) | ... |
| User calls `free(ptr)` | `OWNED → DROPPED` | Borrow count == 0 |
| User calls `free(ptr)` on moved var | **Compiler error** | Already moved |
| User calls `free(ptr)` again | **Compiler error** | Already DROPPED |
| Scope ends (var already DROPPED) | No action | Prevents double-free |

## 7. Derivation Interaction

Derivation blocks (`:= { ... }`) that appear on functions returning pointers interact with early free:
- If a function is marked `no_derive` and the user frees the result early, standard rules apply.
- If synthesis mode generates a function that returns allocated memory, the generated code should document that the caller owns the result.

## 8. Verification

Testing this design requires:

- **Unit test:** `free(ptr)` → variable state is DROPPED, drop pass skips it
- **Unit test:** double `free(ptr)` → compile error
- **Unit test:** use after `free(ptr)` → compile error
- **Integration test:** `free(vec->data); vec->data = NULL;` → no double-free at scope exit
- **Regression test:** function without `free()` still gets normal drop injection

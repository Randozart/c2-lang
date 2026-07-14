# Phase H: Self-hosting — Making C² Compile Itself

**Date:** 2026-07-13  
**Status:** Plan / Pre-Implementation  
**Duration estimate:** 3–5 full sessions  
**Risk:** High (bootstrap fragility, uncovered language gaps)

---

## 1. What Self-Hosting Means

The C² compiler (`c2c`) is currently written in **C23** and compiled by **GCC**.
Self-hosting means the compiler can compile its **own source code**.

**Three-stage bootstrap procedure:**

```
Stage 1: GCC builds c2c-v1 from C source            (already works)
Stage 2: c2c-v1 compiles c2c source → c2c-v2        (the goal)
Stage 3: c2c-v2 compiles c2c source → c2c-v3        (verification: identical to v2)
```

Stage 2 is the breakthrough. Stage 3 proves the compiler is self-consistent
(v2 and v3 must be byte-identical, or at least functionally identical).

---

## 2. Phase H Work Breakdown

### H.1 — Feature Audit: What C Features Does the Compiler Use?

The entire compiler lives under `src/` (~20 files, ~8000 LOC). Every file
must be audited for C constructs that C² may not fully support.

**Automated audit approach:**
```
gcc -E -dM src/*.c          # All preprocessor macros used
grep -rn '->' src/*.c       # Pointer member access
grep -rn '\.\w' src/*.c     # Struct member access (field reads)
grep -rn 'sizeof' src/*.c   # sizeof operator usage
grep -rn 'goto\|setjmp\|longjmp' src/*.c   # Jumps
grep -rn '\.\.\.' src/*.c   # Variadic functions
grep -rn 'malloc\|calloc\|realloc\|free' src/*.c  # Heap allocation
grep -rn 'FILE\*\|fopen\|fclose\|fread\|fwrite' src/*.c  # File I/O
grep -rn 'memcpy\|memmove\|memset\|memcmp' src/*.c  # Memory ops
grep -rn 'qsort\|bsearch' src/*.c    # C standard library callbacks
grep -rn 'assert\b' src/*.c  # Assertions
grep -rn 'printf\|sprintf\|fprintf\|snprintf' src/*.c  # Formatted I/O
grep -rn 'str\w*(' src/*.c   # String functions
grep -rn 'int32_t\|uint32_t\|int64_t' src/*.c  # Fixed-width types
grep -rn 'enum\b' src/*.c    # Enumerations
grep -rn 'union\b' src/*.c   # Unions
grep -rn '->' src/*.c | grep -v '\\->'  # Pointer member access (non-comment)
```

### H.2 — Language Gap Analysis

For each C feature found in H.1, determine:

| Feature | Status in C² | Action |
|---------|-------------|--------|
| Fixed-width int types (`int32_t`) | Supported via `type.h` parser | OK |
| Pointers (`int32_t*`, `void*`) | Partially supported | Need void* support |
| Pointer arithmetic (`p + n`, `p[n]`) | Not tested | Need to add/fix |
| Struct member access (`.` and `->`) | NODE_MEMBER, NODE_DEREF exist but may be incomplete | Need to verify/fix |
| Function pointers | Nonexistent | Compiler doesn't use them heavily — rewrite to avoid |
| Variadic functions (`...`) | Nonexistent | Compiler uses `errlist_add(ERROR_LEVEL_ERROR, ...)` which is variadic |
| Macros (`#define`, `#ifdef`) | Partial | `#include` supported, `#define` parsed but semantics limited |
| `sizeof` | NODE_SIZEOF exists | Verify codegen |
| `goto` | Explicitly excluded | Compiler must avoid it (already does — it's not used) |
| `switch/case` | Supported | OK |
| Heap allocation (`malloc`/`free`) | Supported via `free()` drop | Need `malloc` support |
| File I/O (`fopen`, `fread`, etc.) | Not supported | Need built-in or extern C import |
| String functions (`strcmp`, `strlen`) | Not supported | Need extern C import |
| `printf` family | Not supported | Need extern C import |
| `assert` | Not supported | Simple: define as empty or use when-guard |
| Command-line args (`argc`, `argv`) | Not tested | Verify support |
| `extern` declarations | NODE_EXTERN exists | Verify codegen |
| `const` qualifier | Ignored by parser | OK (stripped silently) |
| `static` functions | Not supported | Need to add (or use naming convention) |
| Header `#include` | Supported | OK |
| `void*` | Not supported as type | Need to add |
| `NULL` | Not a keyword | Define as `0` in c2.h |
| `struct` forward declarations | Parsed | Need to verify codegen |
| Enum with specified values (`enum { A = 1 }`) | Parsed | Need to verify codegen |
| Compound literals | Not supported | Not used in compiler |
| Designated initializers | Not supported | Not used in compiler |
| Inline functions | Not supported | Not used in compiler |

### H.3 — Prioritized Feature Gap Additions

Listed in order of necessity (most critical first):

#### H.3.1 — C standard library bindings (`c2.h` expansion)

The compiler heavily uses:
- `printf`, `fprintf`, `snprintf`, `sprintf` — formatted output (error reporting, codegen output)
- `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell` — file I/O
- `malloc`, `calloc`, `realloc`, `free` — heap allocation (used EVERYWHERE)
- `strcmp`, `strncmp`, `strlen`, `strdup`, `strchr`, `strstr` — string operations
- `memcpy`, `memset`, `memmove` — memory operations
- `exit`, `abort` — process control
- `assert` — debugging (can be replaced)

**Solution:** Expand `include/c2.h` with extern declarations for these
standard C functions so C² code can call them directly.

Example:
```c
// In include/c2.h:
extern int printf(const char* fmt, ...);
extern void* malloc(uint64_t size);
extern void free(void* ptr);
extern int strcmp(const char* a, const char* b);
```

This requires C²'s parser to handle:
- `extern` declarations with complex types
- Function pointer parameters in extern declarations
- `...` (variadic) in extern declarations at minimum (the parser can
  just skip variadic args — they're calling conventions, not type-checked)
- `void*` type

#### H.3.2 — `void*` support

The compiler uses `void*` extensively:
- `z3.h` API returns `Z3_ast` which is `void*`
- `calloc` returns `void*`
- Hash table buckets are `void*`

**Solution:** Add `void` as a type in the parser and typechecker with
special pointer rules:
- `void*` is assignable from/to any pointer type
- `void` (non-pointer) is only valid for function returns
- Codegen emits `void*` correctly

(Actually, `void*` support may already partially work — the parser has
`TOK_VOID` in `is_type_token` and `type_from_tok` maps it to `TYPE_VOID`.
But `void*` (pointer to void) may not be handled.)

#### H.3.3 — Static functions and variables

The compiler uses `static` extensively for file-scope linkage.
Currently the parser accepts `static` but the codegen may not emit it.

**Solution:** Add `NODE_STATIC` or handle `static` keyword in declarations.

#### H.3.4 — Struct member access (`.` and `->`)

The compiler accesses struct members extensively:
```c
node->kind = NODE_FUNCTION;
parser.lexer.current_tok ...
```
These are parsed as `NODE_MEMBER` and `NODE_DEREF`. The codegen emits
them, but the typechecker assigns `TYPE_INVALID` to member access nodes
(currently stubbed as "Phase E work"). For self-hosting, we need basic
member access to typecheck correctly enough for codegen.

**Solution:** For struct member access, skip type-checking and just set
the node type to `TYPE_INVALID` (which codegen treats as "emit anyway").
The codegen already works — it just emits `.name` or `->name`. The
typechecker just needs to not **error** on these nodes.

#### H.3.5 — `const` qualifier in pointer types

The compiler source uses `const char*`, `const void*`, etc. The parser
currently strips `const` by consuming it during `parse_type` (it loops
over all type keywords and keeps only the last one). So `const char*`
becomes `char*` which is mostly fine — C allows assigning `char*` to
`const char*` without warning in most cases.

**Risk:** If the emitted C code loses `const` qualifiers, GCC may warn
(and with `-Werror`, fail). Mitigation: emit `const` when present, or
pass `-Wno-discarded-qualifiers` during bootstrap.

### H.4 — Rewriting Strategy

Each `src/*.c` file must be rewritten from C23 to C² syntax.
The key changes per file type:

#### H.4.1 Parser/type declarations

**C23:**
```c
static int hash_str(const char* s) {
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + c;
    return (int)(h & 0x7FFFFFFF);
}
```

**C²:**
```c
[1][1]
int32_t hash_str(const char* s) {
    uint32_t h = 5381;
    while (*s != 0) {
        h = ((h << 5) + h) + (uint32_t)(*s);
        s = s + 1;
    }
    return (int32_t)(h & 0x7FFFFFFF);
}
```

Key differences:
- Every function needs a contract `[1][1]` or equivalent
- Pointer arithmetic `*s++` → `*s; s = s + 1`
- For loop over pointer: not available, use while
- Casts: `(int)` → `(int32_t)`
- `unsigned long` → `uint64_t` or `uint32_t`
- Implicit int → explicit int32_t
- `&&` and `||` work the same

#### H.4.2 Struct definitions

**C23:**
```c
typedef struct {
    char*   output;
    size_t  output_len;
    int     indent_level;
    int     skip_main;
    ErrorList* errors;
} Codegen;
```

**C²:**
```c
struct Codegen_t {
    char*   output;
    size_t  output_len;
    int32_t indent_level;
    int32_t skip_main;
    ErrorList* errors;
};
typedef struct Codegen_t Codegen;
```

C² supports `struct` and `typedef` (verified in codegen tests).

#### H.4.3 Enum definitions

**C23:**
```c
typedef enum { TOK_PLUS, TOK_MINUS, TOK_STAR, ... } TokenKind;
```

**C²: Must use integer constants instead.**

```c
#define TOK_PLUS 1
#define TOK_MINUS 2
#define TOK_STAR 3
```

Reason: C²'s enum support may not handle the large enums used in the
lexer (60+ values). The codegen test for enum is minimal. Using
`#define` constants is safer and more portable.

### H.5 — File-by-File Rewrite Order

The dependency order for rewriting (a file can only be rewritten after
all its dependencies):

```
1. include/c2.h              — Standard library bindings (NO dependencies)
2. src/error.h               — Error list types
3. src/state.h               — Variable state enum (use #define constants)
4. src/type.h                — Type system header
5. src/ast.h                 — AST node types
6. src/symbol.h              — Symbol table header
7. src/lexer.h + src/lexer.c — Tokenizer (depends on: error.h)
8. src/parser.h + src/parser.c — Parser (depends on: lexer, ast, error)
9. src/state.c               — State machine implementation
10. src/type.c               — Type system implementation
11. src/symbol.c             — Symbol table implementation
12. src/ast.c                — AST construction
13. src/error.c              — Error list implementation
14. src/typecheck.c          — Type checker (depends on: symbol, type, ast)
15. src/vrp.c                — VRP pass
16. src/borrow.c             — Borrow checker
17. src/drop.c               — Drop injection
18. src/codegen.c            — Code generation (depends on: ast)
19. src/verify.c             — Derivation verification
20. src/verifier.c           — Z3 verification
21. src/derive.c             — Synthesis engine
22. src/driver.c             — GCC invocation
23. src/main.c               — CLI entry point
```

Each file should be:
1. Renamed from `src/foo.c` to `src/foo.c2` (C² source)
2. The C23 code is translated to C² syntax
3. A wrapper `src/foo.c` is generated (or the build system is updated
   to compile `.c2` files through `c2c`)

### H.6 — Bootstrap Procedure

```
Step 1: Verify GCC builds c2c-v1 from all .c files
    make clean && make build     → build/c2c (v1)

Step 2: Create .c2 → .c compilation rule in Makefile
    Add: $(BUILD_DIR)/%.c2c: src/%.c2
             ./build/c2c build $< --emit-c -o $@

Step 3: Rewrite one file at a time from C to C²
    Start with simplest: error.c → error.c2
    Compile: error.c2 → error.c2c (C output)
    Compare output to original error.c logic

Step 4: Once ALL files are rewritten, build c2c-v2
    make clean && make build     → build/c2c (v2, built by v1 from .c2 files)

Step 5: Build c2c-v3 from the same .c2 files using v2
    ./build/c2c build src/main.c2 --emit-c → ...
    → build/c2c (v3)

Step 6: Verify v2 and v3 are functionally identical
    make test
    # Both should produce identical test results

Step 7: Celebration — C² is self-hosted!
```

### H.7 — Fallback Strategies

If the full rewrite is too large:

**Fallback A — Hybrid bootstrap:**
Keep the core infrastructure (lexer, parser, ast) in C23 and only
rewrite the higher-level passes (typecheck, borrow, codegen) in C².
The compiler is "mostly self-hosting" — all analysis passes run C² code.

**Fallback B — Source-to-source translation script:**
Write a Python script that does mechanical C23 → C² translation.
This handles the tedious parts (adding contracts, fixing pointer
arithmetic, converting types) automatically. Manual fixes are needed
only for the tricky parts.

**Fallback C — Virtual bootstrap:**
Instead of rewriting source files, modify the build system so that
`make build` compiles the compiler with itself in a single step
(using a fat binary approach). This is harder to debug but proves
the concept faster.

### H.8 — Testing Strategy

For each rewritten file:

```
1. Compile original .c → .o with GCC                  (baseline)
2. Translate .c2 → .c2c with c2c-v1                   (transpiled)
3. Compile .c2c → .o with GCC                         (verify C syntax)
4. Link with other .o files → test binary              (integration)
5. Run test binary, compare output to baseline          (correctness)

Checklist:
- [ ] Compiles without errors
- [ ] Emitted C is valid C23 (gcc -std=c23 -Wall -Werror)
- [ ] All unit tests for this module pass
- [ ] All integration tests still pass
```

### H.9 — File: `include/c2.h` Expansion

The `c2.h` header needs to declare all standard library functions
that C² code can call:

```c
// c2.h — C² standard library bindings
// Automatically included by the C² transpiler in emitted C output.

#ifndef C2_H
#define C2_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <z3.h>

// Memory
extern void* malloc(uint64_t size);
extern void* calloc(uint64_t nmemb, uint64_t size);
extern void* realloc(void* ptr, uint64_t size);
extern void  free(void* ptr);

// I/O
extern int32_t printf(const char* fmt, ...);
extern int32_t fprintf(FILE* stream, const char* fmt, ...);
extern int32_t snprintf(char* buf, uint64_t size, const char* fmt, ...);
extern int32_t sprintf(char* buf, const char* fmt, ...);

// File operations
extern FILE* fopen(const char* path, const char* mode);
extern int32_t fclose(FILE* stream);
extern uint64_t fread(void* ptr, uint64_t size, uint64_t nmemb, FILE* stream);
extern int32_t fseek(FILE* stream, int64_t offset, int32_t whence);
extern int64_t ftell(FILE* stream);
extern int32_t fflush(FILE* stream);

// String
extern int32_t   strcmp(const char* a, const char* b);
extern int32_t   strncmp(const char* a, const char* b, uint64_t n);
extern uint64_t  strlen(const char* s);
extern char*     strdup(const char* s);
extern char*     strchr(const char* s, int32_t c);
extern char*     strstr(const char* haystack, const char* needle);
extern void*     memcpy(void* dest, const void* src, uint64_t n);
extern void*     memset(void* s, int32_t c, uint64_t n);
extern int32_t   memcmp(const void* a, const void* b, uint64_t n);

// Process
extern void exit(int32_t status);
extern int32_t system(const char* cmd);

// Z3 (if available)
// These are declared in z3.h which is included above

#endif
```

### H.10 — Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Missing feature discovered mid-rewrite | High | High | Keep original .c file as fallback; fix feature; resume |
| Bootstrap fails silently (wrong output) | Medium | Critical | Stage 3 verification catches this |
| Enum translation (60+ tokens) tedious | High | Medium | Use Python script to automate |
| Z3 C API headers not parseable by C² | Medium | Medium | Wrap Z3 calls in extern C shim (compiled by GCC) |
| Variadic args (`...`) not parseable | High | High | Skip variadic args in extern decls; use `vprintf`-style instead |
| Code bloat from rewritten files | Low | Low | Focus on correctness first; optimize later |
| `void*` pointer arithmetic breaks | Medium | Medium | Use `(int64_t)(uint64_t)ptr` cast to bypass type system |

### H.11 — Success Criteria

```
☐ make test passes with 180+ tests
☐ make build produces c2c-v2 using only c2c-v1 and .c2 source files
☐ ./build/c2c-v2 build src/main.c2 --emit-c produces valid C23
☐ make test with c2c-v2 gives identical results to v1
☐ All unit tests, integration tests, and verifier tests pass
☐ examples/test_minimal.c2 and examples/swap_bytes.c2 compile and run
```

---

## Summary

Phase H is the most complex and highest-risk phase. The key challenges are:

1. **C library bindings** — exposing printf, malloc, etc. to C² code
2. **Feature gaps** — void*, variadic args, static, const
3. **File-by-file rewrite** — translating ~8000 LOC from C23 to C²
4. **Bootstrap debugging** — when v2 produces wrong output, diagnosing
   whether the bug is in v1 (the C² compiler) or in the rewritten source

The recommended approach is **incremental**: rewrite one module at a time,
test it thoroughly, then move to the next. The dependency order in H.5
ensures each module can be tested in isolation before its dependents
are rewritten.

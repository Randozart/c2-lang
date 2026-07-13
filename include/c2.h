// c2.h — Standard library and compatibility header for C² (Contract Enforced C).
//
// When compiled by the c2 compiler, contracts and attributes are handled
// natively by the parser. When compiled by a standard C compiler (GCC, Clang),
// this header ensures that C²-specific constructs degrade gracefully:
//   - C23 attributes in the `brief` namespace are silently ignored per the
//     C23 standard (§6.7.12) — compilers MUST ignore unrecognized attributes.
//   - Older compilers use empty macro definitions to strip them.
//   - The `cleanup` attribute (GCC/Clang extension) is provided as C2_DEFER()
//     for automatic destructor calls when compiled without c2.
//
// 2026-07-13 — Initial version.

#ifndef C2_H
#define C2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ── C23 attribute compatibility ─────────────────────────────────────────
// Standard C23 compilers (GCC 13+, Clang 18+) must ignore attributes in
// unrecognized namespaces. Additionally, we provide empty macro fallbacks
// for C99/C11 compilers and older GCC/Clang versions.

#if !defined(__c2__) && !defined(__STDC_VERSION__)
    // C89/C99 — strip all attributes
    #define [[c2::pre(x)]]
    #define [[c2::post(x)]]
    #define [[c2::drop(x)]]
    #define [[c2::borrow(x)]]
#elif !defined(__c2__) && defined(__STDC_VERSION__) && __STDC_VERSION__ < 202311L
    // C11 or earlier — strip attributes via empty macros
    #define [[c2::pre(x)]]
    #define [[c2::post(x)]]
    #define [[c2::drop(x)]]
    #define [[c2::borrow(x)]]
#endif

// ── GCC/Clang cleanup attribute for automatic drop ──────────────────────
// When a struct uses [[c2::drop(fn)]], the c2 compiler handles
// destructor injection natively. For standard compilers, this opt-in macro
// uses GCC/Clang's __attribute__((cleanup)) extension, which automatically
// calls `fn` when the variable goes out of scope.

#ifdef __GNUC__
    #define C2_DEFER(drop_fn) __attribute__((cleanup(drop_fn)))
#else
    #define C2_DEFER(drop_fn)
#endif

// ── Platform detection ──────────────────────────────────────────────────

#if defined(__GNUC__) || defined(__clang__)
    #define C2_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define C2_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define C2_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define C2_LIKELY(x)   (x)
    #define C2_UNLIKELY(x) (x)
    #define C2_UNREACHABLE() __assume(0)
#else
    #define C2_LIKELY(x)   (x)
    #define C2_UNLIKELY(x) (x)
    #define C2_UNREACHABLE() ((void)0)
#endif

// ── Utility macros ──────────────────────────────────────────────────────

/// Compute the number of elements in a static array.
#define C2_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/// Assert that a pointer is non-null (becomes a no-op when proven by c2).
#define C2_NONNULL(p) assert((p) != NULL)

/// Mark a parameter as intentionally unused.
#define C2_UNUSED(v) ((void)(v))

// ── Optional: C2 integer types (already in stdint.h, re-exported for convenience) ──

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

#endif // C2_H

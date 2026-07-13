// 2026-07-13 — Type system for C².
//   Defines TypeKind, struct Type, and the type construction/comparison API.
//   Every AstNode carries a Type* populated by the type checker pass.

#ifndef C2_TYPE_H
#define C2_TYPE_H

#include <stddef.h>
#include <stdint.h>

typedef struct Symbol Symbol;

typedef enum {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_UINT32,
    TYPE_UINT64,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_ENUM,
    TYPE_NAMED,
    TYPE_INVALID,
} TypeKind;

typedef struct Type {
    TypeKind       kind;
    int            is_signed;     // For integer types
    size_t         bit_width;     // For integer types

    struct Type*   subtype;       // Element type (pointer/array), return type (function)

    struct Type**  param_types;   // Function param types (heap-allocated)
    size_t         param_count;

    size_t         array_size;    // 0 = incomplete / unknown

    const char*    name;          // Struct/union/enum/typedef name
    Symbol*        struct_sym;    // Struct/union/enum symbol table entry
} Type;

// ── Construction ─────────────────────────────────────────────────────────

Type* type_primitive(TypeKind kind);
Type* type_pointer(Type* element);
Type* type_array(Type* element, size_t size);
Type* type_function(Type* ret, Type** params, size_t n);
Type* type_named(const char* name, Type* underlying);
Type* type_deep_copy(const Type* src);

// ── Queries ──────────────────────────────────────────────────────────────

int type_is_integer(const Type* t);
int type_is_floating(const Type* t);
int type_is_arithmetic(const Type* t);
int type_is_scalar(const Type* t);
int type_is_pointer(const Type* t);
int type_is_void(const Type* t);
int type_is_error(const Type* t);
int type_is_bool(const Type* t);
int type_is_signed(const Type* t);
int type_is_unsigned(const Type* t);
size_t type_sizeof(const Type* t);  // Rough size in bytes

// ── Comparison ───────────────────────────────────────────────────────────

int type_equal(const Type* a, const Type* b);
int type_assignable(const Type* dest, const Type* src);

// ── Formatting ───────────────────────────────────────────────────────────

const char* type_kind_name(TypeKind kind);
const char* type_to_string(const Type* t, char* buf, size_t bufsz);

// ── Cleanup ──────────────────────────────────────────────────────────────

void type_free(Type* t);

#endif // C2_TYPE_H

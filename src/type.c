// 2026-07-13 — Type system implementation for C².
//   Construction, queries, comparison, formatting, cleanup.

#include "type.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// ── Helpers ──────────────────────────────────────────────────────────────

static Type* type_alloc(TypeKind kind) {
    Type* t = (Type*)calloc(1, sizeof(Type));
    if (!t) return NULL;
    t->kind = kind;
    t->is_signed = 1;
    return t;
}


static int integer_bit_width(TypeKind kind) {
    switch (kind) {
    case TYPE_INT8:   case TYPE_UINT8:   return 8;
    case TYPE_INT16:  case TYPE_UINT16:  return 16;
    case TYPE_INT32:  case TYPE_UINT32:  return 32;
    case TYPE_INT64:  case TYPE_UINT64:  return 64;
    default: return 0;
    }
}

static int integer_is_signed(TypeKind kind) {
    switch (kind) {
    case TYPE_INT8: case TYPE_INT16: case TYPE_INT32: case TYPE_INT64:
        return 1;
    default: return 0;
    }
}

// ── Construction ─────────────────────────────────────────────────────────

Type* type_primitive(TypeKind kind) {
    Type* t = type_alloc(kind);
    if (!t) return NULL;
    t->bit_width = integer_bit_width(kind);
    t->is_signed = integer_is_signed(kind);
    return t;
}

Type* type_pointer(Type* element) {
    Type* t = type_alloc(TYPE_POINTER);
    if (!t) return NULL;
    t->subtype = element;
    t->bit_width = 64; // assume 64-bit target
    return t;
}

Type* type_array(Type* element, size_t size) {
    Type* t = type_alloc(TYPE_ARRAY);
    if (!t) return NULL;
    t->subtype = element;
    t->array_size = size;
    return t;
}

/// Deep-copy a type tree. The caller owns the returned copy.
Type* type_deep_copy(const Type* src) {
    if (!src) return NULL;
    if (src->kind >= TYPE_VOID && src->kind <= TYPE_DOUBLE) {
        return type_primitive(src->kind);
    }
    if (src->kind == TYPE_POINTER) {
        return type_pointer(type_deep_copy(src->subtype));
    }
    if (src->kind == TYPE_ARRAY) {
        return type_array(type_deep_copy(src->subtype), src->array_size);
    }
    Type* t = (Type*)calloc(1, sizeof(Type));
    if (!t) return NULL;
    t->kind = src->kind;
    t->bit_width = src->bit_width;
    t->is_signed = src->is_signed;
    t->array_size = src->array_size;
    t->struct_sym = src->struct_sym;
    if (src->name) t->name = strdup(src->name);
    t->subtype = type_deep_copy(src->subtype);
    if (src->param_count > 0 && src->param_types) {
        t->param_count = src->param_count;
        t->param_types = (Type**)calloc(t->param_count, sizeof(Type*));
        for (size_t i = 0; i < t->param_count; i++) {
            t->param_types[i] = type_deep_copy(src->param_types[i]);
        }
    }
    return t;
}

Type* type_function(Type* ret, Type** params, size_t n) {
    Type* t = type_alloc(TYPE_FUNCTION);
    if (!t) return NULL;
    t->subtype = ret;
    t->param_count = n;
    if (n > 0) {
        t->param_types = (Type**)calloc(n, sizeof(Type*));
        if (!t->param_types) { free(t); return NULL; }
        for (size_t i = 0; i < n; i++) {
            t->param_types[i] = type_deep_copy(params[i]);
        }
    }
    return t;
}

Type* type_named(const char* name, Type* underlying) {
    Type* t = type_alloc(TYPE_NAMED);
    if (!t) return NULL;
    t->name = name ? strdup(name) : NULL;
    t->subtype = underlying;
    return t;
}


// ── Queries ──────────────────────────────────────────────────────────────

int type_is_integer(const Type* t) {
    if (!t) return 0;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_INT8:   case TYPE_INT16:  case TYPE_INT32:  case TYPE_INT64:
    case TYPE_UINT8:  case TYPE_UINT16: case TYPE_UINT32: case TYPE_UINT64:
        return 1;
    default: return 0;
    }
}

int type_is_floating(const Type* t) {
    if (!t) return 0;
    return t->kind == TYPE_FLOAT || t->kind == TYPE_DOUBLE;
}

int type_is_arithmetic(const Type* t) {
    return type_is_integer(t) || type_is_floating(t);
}

int type_is_scalar(const Type* t) {
    if (!t) return 0;
    return type_is_arithmetic(t) || type_is_pointer(t);
}

int type_is_pointer(const Type* t) {
    if (!t) return 0;
    return t->kind == TYPE_POINTER;
}

int type_is_void(const Type* t) {
    if (!t) return 0;
    return t->kind == TYPE_VOID;
}

int type_is_error(const Type* t) {
    if (!t) return 1;
    return t->kind == TYPE_INVALID;
}

int type_is_bool(const Type* t) {
    if (!t) return 0;
    return t->kind == TYPE_BOOL;
}

int type_is_signed(const Type* t) {
    if (!t) return 0;
    return t->is_signed;
}

int type_is_unsigned(const Type* t) {
    if (!t) return 0;
    return t->kind != TYPE_BOOL && type_is_integer(t) && !t->is_signed;
}

size_t type_sizeof(const Type* t) {
    if (!t) return 0;
    switch (t->kind) {
    case TYPE_VOID:    return 0;
    case TYPE_BOOL:    return 1;
    case TYPE_INT8:   case TYPE_UINT8:   return 1;
    case TYPE_INT16:  case TYPE_UINT16:  return 2;
    case TYPE_INT32:  case TYPE_UINT32:  return 4;
    case TYPE_INT64:  case TYPE_UINT64:  case TYPE_DOUBLE: return 8;
    case TYPE_FLOAT:   return 4;
    case TYPE_POINTER: return 8;
    case TYPE_ARRAY:
        if (!t->subtype) return 0;
        return t->array_size * type_sizeof(t->subtype);
    case TYPE_NAMED:
        if (t->subtype) return type_sizeof(t->subtype);
        return 0;
    default: return 0;
    }
}

// ── Comparison ───────────────────────────────────────────────────────────

int type_equal(const Type* a, const Type* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case TYPE_VOID: case TYPE_BOOL:
    case TYPE_INT8:  case TYPE_INT16: case TYPE_INT32: case TYPE_INT64:
    case TYPE_UINT8: case TYPE_UINT16: case TYPE_UINT32: case TYPE_UINT64:
    case TYPE_FLOAT: case TYPE_DOUBLE:
        return a->bit_width == b->bit_width && a->is_signed == b->is_signed;

    case TYPE_POINTER:
        return type_equal(a->subtype, b->subtype);

    case TYPE_ARRAY:
        return a->array_size == b->array_size && type_equal(a->subtype, b->subtype);

    case TYPE_FUNCTION: {
        if (a->param_count != b->param_count) return 0;
        if (!type_equal(a->subtype, b->subtype)) return 0;
        for (size_t i = 0; i < a->param_count; i++) {
            if (!type_equal(a->param_types[i], b->param_types[i])) return 0;
        }
        return 1;
    }

    case TYPE_STRUCT: case TYPE_UNION: case TYPE_ENUM:
        if (!a->name || !b->name) return a->struct_sym == b->struct_sym;
        return strcmp(a->name, b->name) == 0;

    case TYPE_NAMED:
        if (a->name && b->name) return strcmp(a->name, b->name) == 0;
        return type_equal(a->subtype, b->subtype);

    default:
        return a == b;
    }
}

int type_assignable(const Type* dest, const Type* src) {
    if (!dest || !src) return 0;
    if (type_is_error(dest) || type_is_error(src)) return 1;

    // Exact match
    if (type_equal(dest, src)) return 1;

    // void* is assignable to any pointer and vice versa
    if (type_is_pointer(dest) && type_is_pointer(src)) {
        if (type_is_void(dest->subtype) || type_is_void(src->subtype)) return 1;
    }

    // Integer promotions: smaller -> larger signed/unsigned OK
    if (type_is_integer(dest) && type_is_integer(src)) return 1;

    // Float <-> double implicit conversion
    if (type_is_floating(dest) && type_is_floating(src)) return 1;

    // Integer <-> float implicit conversion
    if (type_is_arithmetic(dest) && type_is_arithmetic(src)) return 1;

    // Array decays to pointer to element
    if (src->kind == TYPE_ARRAY && type_is_pointer(dest)) {
        return type_equal(src->subtype, dest->subtype);
    }

    // Named types: unwrap
    if (src->kind == TYPE_NAMED) return type_assignable(dest, src->subtype);
    if (dest->kind == TYPE_NAMED) return type_assignable(dest->subtype, src);

    return 0;
}

// ── Formatting ───────────────────────────────────────────────────────────

const char* type_kind_name(TypeKind kind) {
    switch (kind) {
    case TYPE_VOID:    return "void";
    case TYPE_BOOL:    return "bool";
    case TYPE_INT8:    return "int8_t";
    case TYPE_INT16:   return "int16_t";
    case TYPE_INT32:   return "int32_t";
    case TYPE_INT64:   return "int64_t";
    case TYPE_UINT8:   return "uint8_t";
    case TYPE_UINT16:  return "uint16_t";
    case TYPE_UINT32:  return "uint32_t";
    case TYPE_UINT64:  return "uint64_t";
    case TYPE_FLOAT:   return "float";
    case TYPE_DOUBLE:  return "double";
    case TYPE_POINTER: return "pointer";
    case TYPE_ARRAY:   return "array";
    case TYPE_FUNCTION: return "function";
    case TYPE_STRUCT:  return "struct";
    case TYPE_UNION:   return "union";
    case TYPE_ENUM:    return "enum";
    case TYPE_NAMED:   return "typedef";
    case TYPE_INVALID: return "<invalid>";
    default:           return "<unknown>";
    }
}

const char* type_to_string(const Type* t, char* buf, size_t bufsz) {
    if (!t || bufsz == 0) return "";
    if (type_is_error(t)) { snprintf(buf, bufsz, "<error>"); return buf; }

    switch (t->kind) {
    case TYPE_POINTER: {
        char inner[128];
        type_to_string(t->subtype, inner, sizeof(inner));
        snprintf(buf, bufsz, "%s*", inner);
        return buf;
    }
    case TYPE_ARRAY: {
        char inner[128];
        type_to_string(t->subtype, inner, sizeof(inner));
        if (t->array_size > 0)
            snprintf(buf, bufsz, "%s[%zu]", inner, t->array_size);
        else
            snprintf(buf, bufsz, "%s[]", inner);
        return buf;
    }
    case TYPE_FUNCTION: {
        char ret_str[64];
        type_to_string(t->subtype, ret_str, sizeof(ret_str));
        size_t pos = snprintf(buf, bufsz, "%s (*)(", ret_str);
        for (size_t i = 0; i < t->param_count && pos < bufsz; i++) {
            char pstr[64];
            type_to_string(t->param_types[i], pstr, sizeof(pstr));
            if (i > 0) pos += snprintf(buf + pos, bufsz - pos, ", ");
            pos += snprintf(buf + pos, bufsz - pos, "%s", pstr);
        }
        if (pos < bufsz) snprintf(buf + pos, bufsz - pos, ")");
        return buf;
    }
    case TYPE_STRUCT:
    case TYPE_UNION:
    case TYPE_ENUM:
        if (t->name) { snprintf(buf, bufsz, "%s %s", type_kind_name(t->kind), t->name); return buf; }
        return type_kind_name(t->kind);
    case TYPE_NAMED:
        if (t->name) { snprintf(buf, bufsz, "%s", t->name); return buf; }
        snprintf(buf, bufsz, "%s", type_kind_name(t->kind));
        return buf;
    default:
        snprintf(buf, bufsz, "%s", type_kind_name(t->kind));
        return buf;
    }
}

// ── Cleanup ──────────────────────────────────────────────────────────────

void type_free(Type* t) {
    if (!t) return;
    if (t->param_types) {
        for (size_t i = 0; i < t->param_count; i++) {
            type_free(t->param_types[i]);
        }
        free(t->param_types);
    }
    type_free(t->subtype);
    if (t->kind == TYPE_NAMED) free((void*)t->name);
    free(t);
}

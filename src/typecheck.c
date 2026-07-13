// 2026-07-13 — Type checker pass for C².
//   Walks the AST, annotates every expression node with its Type*,
//   and validates type correctness per C23 rules + C² extensions.

#include "typecheck.h"
#include "type.h"
#include "symbol.h"
#include "error.h"
#include "lexer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// ── Context ──────────────────────────────────────────────────────────────

typedef struct {
    SymbolTable* symtab;
    ErrorList*   errors;
    Type*        current_return_type;  // For return type checking (NULL = void)
    int          in_postcondition;      // For `result` checking
    int          has_type_error;        // Accumulated result
} TCContext;

// ── Forward declarations ─────────────────────────────────────────────────

static void tc_node(AstNode* node, TCContext* ctx);

// ── Type tree clearance (forces re-type-checking) ────────────────────────

/// Recursively clear node->type on an entire sub-tree.
/// Used to force re-type-checking (e.g. contract expressions after params
/// are registered).
static void clear_type_tree(AstNode* node) {
    if (!node) return;
    node->type = NULL;
    for (size_t i = 0; i < node->child_count; i++) {
        clear_type_tree(node->children[i]);
    }
}
static Type* type_from_tok(Token tok);

// ── Type name resolution ─────────────────────────────────────────────────

static Type* type_from_tok(Token tok) {
    int text_len = (int)tok.len;
    const char* text = tok.text;
    if (!text) return type_primitive(TYPE_INVALID);

    // Struct/Union/Enum — use the second child which has the tag name
    if (tok.kind == TOK_STRUCT || tok.kind == TOK_UNION || tok.kind == TOK_ENUM) {
        return NULL; // Handled at call site via children
    }

    // Check known type names
    struct { const char* name; TypeKind kind; } primitives[] = {
        {"void", TYPE_VOID},
        {"bool", TYPE_BOOL},
        {"_Bool", TYPE_BOOL},
        {"int8_t", TYPE_INT8},
        {"int16_t", TYPE_INT16},
        {"int32_t", TYPE_INT32},
        {"int64_t", TYPE_INT64},
        {"uint8_t", TYPE_UINT8},
        {"uint16_t", TYPE_UINT16},
        {"uint32_t", TYPE_UINT32},
        {"uint64_t", TYPE_UINT64},
        {"int", TYPE_INT32},       // Default int → int32_t
        {"unsigned", TYPE_UINT32}, // Default unsigned → uint32_t
        {"float", TYPE_FLOAT},
        {"double", TYPE_DOUBLE},
        {"char", TYPE_INT8},       // char → int8_t
        {"short", TYPE_INT16},     // short → int16_t
    };

    for (size_t i = 0; i < sizeof(primitives)/sizeof(primitives[0]); i++) {
        if ((int)strlen(primitives[i].name) == text_len &&
            memcmp(text, primitives[i].name, (size_t)text_len) == 0) {
            return type_primitive(primitives[i].kind);
        }
    }

    return NULL; // Not a built-in type (could be typedef name)
}

// ── Parse a TYPE from a type AST node ────────────────────────────────────
// A "type node" in the AST is either:
//   - A NODE_VARIABLE with a type name token
//   - A NODE_VARIABLE for struct/union/enum with a name child
//   - A pointer chain: parse_type returns NODE_VARIABLE, but pointer depth
//     is indicated by multiple stars after the type name.

static Type* parse_type_node(AstNode* type_node, TCContext* ctx) {
    if (!type_node) return type_primitive(TYPE_VOID);

    // Base type: look at the token
    Type* base = type_from_tok(type_node->token);
    if (!base) {
        // Not a built-in — try typedef lookup
        char type_name[256];
        int text_len = (int)type_node->token.len;
        if (text_len > 255) text_len = 255;
        snprintf(type_name, sizeof(type_name), "%.*s", text_len, type_node->token.text);

        Symbol* sym = symtab_lookup(ctx->symtab, type_name);
        if (sym && sym->type) {
            // Create a named type wrapper
            base = type_named(strdup(type_name), sym->type);
        } else {
            // Unknown type — mark as invalid
            errlist_add(ctx->errors, ERROR_LEVEL_ERROR, type_node->token.loc,
                        "unknown type '%.*s'", text_len, type_node->token.text);
            ctx->has_type_error = 1;
            return type_primitive(TYPE_INVALID);
        }
    }

    // Handle struct/union/enum with tag name child
    if (type_node->child_count > 0 &&
        type_node->children[0]->kind == NODE_VARIABLE) {
        // Check if it's a struct/union/enum tag
        if (type_node->token.kind == TOK_STRUCT) {
            Type* st = type_primitive(TYPE_STRUCT);
            st->name = strndup(type_node->children[0]->token.text,
                               type_node->children[0]->token.len);
            type_free(base);
            // Check pointer depth (child_count > 1 means multiple stars)
            if (type_node->child_count > 1) {
                Type* ptr = type_pointer(st);
                return ptr;
            }
            return st;
        }
        if (type_node->token.kind == TOK_UNION) {
            Type* ut = type_primitive(TYPE_UNION);
            ut->name = strndup(type_node->children[0]->token.text,
                               type_node->children[0]->token.len);
            type_free(base);
            if (type_node->child_count > 1) {
                return type_pointer(ut);
            }
            return ut;
        }
        if (type_node->token.kind == TOK_ENUM) {
            Type* et = type_primitive(TYPE_ENUM);
            et->name = strndup(type_node->children[0]->token.text,
                               type_node->children[0]->token.len);
            type_free(base);
            if (type_node->child_count > 1) {
                return type_pointer(et);
            }
            return et;
        }
    }

    // Handle pointer depth: children with kind NODE_VARIABLE and TOK_STAR
    // encode the pointer depth in their flags field.
    int pointer_depth = 0;
    for (size_t ci = 0; ci < type_node->child_count; ci++) {
        AstNode* child = type_node->children[ci];
        if (child->kind == NODE_VARIABLE && child->token.kind == TOK_STAR) {
            pointer_depth = child->flags;
        }
    }

    // Wrap base type in pointer chain if needed
    Type* result = base;
    for (int i = 0; i < pointer_depth; i++) {
        result = type_pointer(result);
    }

    return result;
}

// ── Check binary operator type compatibility ─────────────────────────────

static Type* common_arithmetic_type(Type* a, Type* b) {
    if (!a || !b) return NULL;
    if (type_is_error(a) || type_is_error(b)) return type_primitive(TYPE_INVALID);

    // Both floating: use larger
    if (type_is_floating(a) && type_is_floating(b)) {
        return (type_sizeof(a) >= type_sizeof(b)) ? a : b;
    }
    // One floating, one integer: float wins
    if (type_is_floating(a) && type_is_integer(b)) return a;
    if (type_is_integer(a) && type_is_floating(b)) return b;

    // Both integer: usual arithmetic conversions (simplified: use larger)
    if (type_is_integer(a) && type_is_integer(b)) {
        size_t asz = type_sizeof(a);
        size_t bsz = type_sizeof(b);
        if (asz > bsz) return a;
        if (bsz > asz) return b;
        // Same size: unsigned wins if one is unsigned
        if (type_is_unsigned(a)) return a;
        return b;
    }

    return NULL; // Incompatible
}

static void check_binary_op(AstNode* node, TCContext* ctx) {
    if (node->child_count < 2) {
        ctx->has_type_error = 1;
        return;
    }
    AstNode* left = node->children[0];
    AstNode* right = node->children[1];
    Type* lt = left->type;
    Type* rt = right->type;

    if (type_is_error(lt) || type_is_error(rt)) {
        node->type = type_primitive(TYPE_INVALID);
        return;
    }

    TokenKind op = node->token.kind;

    // Assignment operators
        if (op == TOK_ASSIGN || op == TOK_ADD_ASSIGN || op == TOK_SUB_ASSIGN ||
            op == TOK_MUL_ASSIGN || op == TOK_DIV_ASSIGN) {
            if (!type_assignable(lt, rt)) {
                char lbuf[64], rbuf[64];
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "assignment type mismatch: '%s' vs '%s'",
                            type_to_string(lt, lbuf, sizeof(lbuf)),
                            type_to_string(rt, rbuf, sizeof(rbuf)));
                ctx->has_type_error = 1;
                node->type = type_deep_copy(lt);
            } else {
                node->type = type_deep_copy(lt);
            }
            return;
        }

        // Comparison operators → bool
        if (op == TOK_EQ || op == TOK_NE || op == TOK_LT || op == TOK_GT ||
            op == TOK_LE || op == TOK_GE) {
            if (!type_is_arithmetic(lt) || !type_is_arithmetic(rt)) {
                if (!type_is_pointer(lt) || !type_is_pointer(rt)) {
                    char lbuf[64], rbuf[64];
                    errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                                "comparison between non-arithmetic types: '%s' vs '%s'",
                                type_to_string(lt, lbuf, sizeof(lbuf)),
                                type_to_string(rt, rbuf, sizeof(rbuf)));
                    ctx->has_type_error = 1;
                }
            }
            node->type = type_primitive(TYPE_BOOL);
            return;
        }

        // Logical operators → bool
        if (op == TOK_AND || op == TOK_OR) {
            node->type = type_primitive(TYPE_BOOL);
            return;
        }

        // Bitwise operators → integer type
        if (op == TOK_BIT_AND || op == TOK_BIT_OR || op == TOK_BIT_XOR ||
            op == TOK_SHL || op == TOK_SHR) {
            if (!type_is_integer(lt) || !type_is_integer(rt)) {
                char lbuf[64], rbuf[64];
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "bitwise operator on non-integer types: '%s' vs '%s'",
                            type_to_string(lt, lbuf, sizeof(lbuf)),
                            type_to_string(rt, rbuf, sizeof(rbuf)));
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            } else {
                node->type = type_deep_copy(common_arithmetic_type(lt, rt));
            }
            return;
        }

        // Arithmetic operators
        Type* common = common_arithmetic_type(lt, rt);
        if (!common) {
            char lbuf[64], rbuf[64];
            errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                        "type mismatch in arithmetic expression: '%s' vs '%s'",
                        type_to_string(lt, lbuf, sizeof(lbuf)),
                        type_to_string(rt, rbuf, sizeof(rbuf)));
            ctx->has_type_error = 1;
            node->type = type_primitive(TYPE_INVALID);
        } else {
            node->type = type_primitive(common->kind);
            node->type->bit_width = common->bit_width;
            node->type->is_signed = common->is_signed;
        }
}

// ── The recursive type checker ───────────────────────────────────────────

static void tc_node(AstNode* node, TCContext* ctx) {
    if (!node) return;

    // First, type-check all children recursively (post-order).
    // Skip contract and body children of function nodes — they will be
    // re-checked inside NODE_FUNCTION handler after params are in scope.
    for (size_t i = 0; i < node->child_count; i++) {
        AstNode* child = node->children[i];
        if (node->kind == NODE_FUNCTION &&
            (child->kind == NODE_CONTRACT_PRE || child->kind == NODE_CONTRACT_POST ||
             child->kind == NODE_BLOCK)) {
            continue;
        }
        tc_node(child, ctx);
    }

    // Skip nodes that already have a type
    if (node->type) return;

    switch (node->kind) {

    // ── Literals ──────────────────────────────────────────────────────
    case NODE_LITERAL_INT: {
        int64_t val = node->token.value.i64;
        if (val >= INT32_MIN && val <= INT32_MAX)
            node->type = type_primitive(TYPE_INT32);
        else
            node->type = type_primitive(TYPE_INT64);
        break;
    }

    case NODE_LITERAL_FLOAT:
        node->type = type_primitive(TYPE_DOUBLE);
        break;

    case NODE_LITERAL_CHAR:
        node->type = type_primitive(TYPE_INT8);
        break;

    case NODE_LITERAL_STR:
        node->type = type_pointer(type_primitive(TYPE_INT8));  // char*
        break;

    // ── Variables ─────────────────────────────────────────────────────
    case NODE_VARIABLE: {
        // Check if it's the special `result` variable in a postcondition
        if (ctx->in_postcondition && node->token.len == 6 &&
            memcmp(node->token.text, "result", 6) == 0) {
            // result type matches return type
            if (ctx->current_return_type && !type_is_void(ctx->current_return_type)) {
                node->type = type_deep_copy(ctx->current_return_type);
            } else {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "'result' used in void function or outside postcondition");
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            }
            break;
        }

        char name[256];
        int text_len = (int)node->token.len;
        if (text_len > 255) text_len = 255;
        snprintf(name, sizeof(name), "%.*s", text_len, node->token.text);

        Symbol* sym = symtab_lookup(ctx->symtab, name);
        if (sym && sym->type) {
            node->symbol = sym;
            if (sym->type->kind == TYPE_FUNCTION) {
                // Function names don't own their type — the declaration node does.
                // Leave node->type NULL; NODE_CALL handler will look up the sym.
            } else {
                node->type = type_deep_copy(sym->type);
            }
        } else {
            // Might be a function call name — leave type NULL for now
            // or report error if not resolved later
            node->type = type_primitive(TYPE_INVALID);
        }
        break;
    }

    // ── Declaration ───────────────────────────────────────────────────
    case NODE_DECL: {
        // Children: [0]=type_node, [1]=name, [2+]=init/array_size
        if (node->child_count < 2) break;

        // Parse the type from the type node
        AstNode* type_node = node->children[0];
        // For array declarations: check if there's an array subscript child
        Type* var_type = NULL;

        // Find array subscript (NODE_ARRAY_SUB) among children
        size_t array_size = 0;
        for (size_t i = 2; i < node->child_count; i++) {
            if (node->children[i]->kind == NODE_ARRAY_SUB &&
                node->children[i]->child_count > 0) {
                AstNode* size_node = node->children[i]->children[0];
                if (size_node->kind == NODE_LITERAL_INT) {
                    array_size = (size_t)size_node->token.value.i64;
                }
            }
        }

        Type* base_type = parse_type_node(type_node, ctx);
        if (type_is_error(base_type)) {
            break;
        }

        if (array_size > 0) {
            var_type = type_array(base_type, array_size);
        } else {
            // Check if type_node has pointer marker (child_count > 1 for struct/union/enum)
            // For primitive pointers, the star is not encoded — handle separately
            if (type_node->token.kind == TOK_STRUCT ||
                type_node->token.kind == TOK_UNION ||
                type_node->token.kind == TOK_ENUM) {
                var_type = base_type; // Already includes pointer if needed
            } else {
                var_type = base_type;
            }
        }

        // Register the variable in the symbol table
        char varname[256];
        int vname_len = (int)node->children[1]->token.len;
        if (vname_len > 255) vname_len = 255;
        snprintf(varname, sizeof(varname), "%.*s", vname_len,
                 node->children[1]->token.text);

        Symbol* existing = symtab_lookup_current(ctx->symtab, varname);
        if (existing) {
            errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                        "redeclaration of '%s'", varname);
            ctx->has_type_error = 1;
        } else {
            symtab_insert(ctx->symtab, varname, var_type, node->token.loc);
        }

        node->type = var_type;
        break;
    }

    // ── Binary ops ────────────────────────────────────────────────────
    case NODE_BINARY_OP:
        check_binary_op(node, ctx);
        break;

    // ── Assignment (NODE_ASSIGN) ───────────────────────────────────────
    case NODE_ASSIGN: {
        if (node->child_count < 2) break;
        AstNode* dest = node->children[0];
        AstNode* src = node->children[1];
        if (!dest->type || !src->type) break;

        if (!type_assignable(dest->type, src->type)) {
            char dbuf[64], sbuf[64];
            errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                        "cannot assign '%s' to '%s'",
                        type_to_string(src->type, sbuf, sizeof(sbuf)),
                        type_to_string(dest->type, dbuf, sizeof(dbuf)));
            ctx->has_type_error = 1;
            node->type = type_primitive(TYPE_INVALID);
        } else {
            node->type = type_deep_copy(dest->type);
        }
        break;
    }

    // ── Unary ops ─────────────────────────────────────────────────────
    case NODE_UNARY_OP: {
        if (node->child_count < 1) break;
        AstNode* operand = node->children[0];
        if (!operand->type) break;

        TokenKind op = node->token.kind;

        // Address-of: &expr → pointer to expr type
        if (op == TOK_BIT_AND) {
            node->type = type_pointer(type_deep_copy(operand->type));
            break;
        }

        // Dereference: *expr → subtype of pointer
        if (op == TOK_STAR) {
            if (type_is_pointer(operand->type)) {
                node->type = type_deep_copy(operand->type->subtype);
            } else {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "dereference of non-pointer type");
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            }
            break;
        }

        // Negation, bitwise not: operand must be arithmetic
        if (op == TOK_MINUS || op == TOK_BIT_NOT) {
            if (!type_is_arithmetic(operand->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "unary operator on non-arithmetic type");
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            } else {
                node->type = type_deep_copy(operand->type);
            }
            break;
        }

        // Logical not: operand can be any scalar, result is bool
        if (op == TOK_NOT) {
            if (!type_is_scalar(operand->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "logical not on non-scalar type");
                ctx->has_type_error = 1;
            }
            node->type = type_primitive(TYPE_BOOL);
            break;
        }

        // Pre-increment/decrement: ++expr, --expr
        if (op == TOK_INC || op == TOK_DEC) {
            if (!type_is_arithmetic(operand->type) && !type_is_pointer(operand->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "increment/decrement on non-arithmetic/pointer type");
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            } else {
                node->type = type_deep_copy(operand->type);
            }
            break;
        }

        // sizeof: result is size_t (uint64_t)
        if (node->token.kind == TOK_SIZEOF) {
            node->type = type_primitive(TYPE_UINT64);
            break;
        }

        node->type = type_deep_copy(operand->type);
        break;
    }

    // ── Ternary ───────────────────────────────────────────────────────
    case NODE_TERNARY: {
        if (node->child_count < 3) break;
        AstNode* cond = node->children[0];
        AstNode* then_expr = node->children[1];
        AstNode* else_expr = node->children[2];

        // Condition must be scalar
        if (!type_is_scalar(cond->type)) {
            errlist_add(ctx->errors, ERROR_LEVEL_ERROR, cond->token.loc,
                        "ternary condition must be scalar");
            ctx->has_type_error = 1;
        }

        // Then/else must have compatible types
        if (then_expr->type && else_expr->type) {
            if (!type_assignable(then_expr->type, else_expr->type) &&
                !type_assignable(else_expr->type, then_expr->type)) {
                char tbuf[64], ebuf[64];
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "ternary branches have incompatible types: '%s' vs '%s'",
                            type_to_string(then_expr->type, tbuf, sizeof(tbuf)),
                            type_to_string(else_expr->type, ebuf, sizeof(ebuf)));
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            } else {
                node->type = type_deep_copy(then_expr->type);
            }
        }
        break;
    }

    // ── Function calls ────────────────────────────────────────────────
    case NODE_CALL: {
        if (node->child_count < 1) break;
        AstNode* callee = node->children[0];
        Type* callee_type = NULL;

        // Look up the function type from the callee name or its node
        if (callee->type && callee->type->kind == TYPE_FUNCTION) {
            callee_type = callee->type;
        } else if (callee->kind == NODE_VARIABLE && callee->symbol) {
            // NODE_VARIABLE handler may leave type NULL for function names
            if (callee->symbol->type->kind == TYPE_FUNCTION) {
                callee_type = callee->symbol->type;
            }
        } else if (callee->kind == NODE_VARIABLE) {
            // Fallback: look up by name
            char fname[256];
            int flen = (int)callee->token.len;
            if (flen > 255) flen = 255;
            snprintf(fname, sizeof(fname), "%.*s", flen, callee->token.text);
            Symbol* sym = symtab_lookup(ctx->symtab, fname);
            if (sym && sym->type && sym->type->kind == TYPE_FUNCTION) {
                callee_type = sym->type;
            }
        }

        if (callee_type) {
            node->type = type_deep_copy(callee_type->subtype); // Owned copy of return type

            // Check parameter count
            size_t arg_count = node->child_count - 1;
            if (arg_count != callee_type->param_count) {
                char fname_buf[256] = "<unknown>";
                if (callee->kind == NODE_VARIABLE) {
                    snprintf(fname_buf, sizeof(fname_buf), "%.*s",
                             (int)callee->token.len, callee->token.text);
                }
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "function '%s' expects %zu arguments but got %zu",
                            fname_buf, callee_type->param_count, arg_count);
                ctx->has_type_error = 1;
            } else {
                // Check parameter types
                for (size_t i = 0; i < arg_count && i < callee_type->param_count; i++) {
                    AstNode* arg = node->children[i + 1];
                    Type* param_type = callee_type->param_types[i];
                    if (!param_type) {
                        errlist_add(ctx->errors, ERROR_LEVEL_ERROR, arg->token.loc,
                                    "argument %zu has unknown parameter type", i + 1);
                        ctx->has_type_error = 1;
                    } else if (arg->type && !type_assignable(param_type, arg->type)) {
                        char abuf[64], pbuf[64];
                        errlist_add(ctx->errors, ERROR_LEVEL_ERROR, arg->token.loc,
                                    "argument %zu type mismatch: expected '%s', got '%s'",
                                    i + 1,
                                    type_to_string(param_type, pbuf, sizeof(pbuf)),
                                    type_to_string(arg->type, abuf, sizeof(abuf)));
                        ctx->has_type_error = 1;
                    }
                }
            }
        } else {
            node->type = type_primitive(TYPE_INVALID);
        }
        break;
    }

    // ── Return ─────────────────────────────────────────────────────────
    case NODE_RETURN: {
        AstNode* expr = (node->child_count > 0) ? node->children[0] : NULL;
        if (ctx->current_return_type) {
            if (type_is_void(ctx->current_return_type)) {
                if (expr) {
                    errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                                "return with value in void function");
                    ctx->has_type_error = 1;
                }
            } else {
                if (!expr) {
                    errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                                "return without value in non-void function");
                    ctx->has_type_error = 1;
                } else if (!type_assignable(ctx->current_return_type, expr->type)) {
                    char rbuf[64], ebuf[64];
                    errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                                "return type mismatch: expected '%s', got '%s'",
                                type_to_string(ctx->current_return_type, rbuf, sizeof(rbuf)),
                                type_to_string(expr->type, ebuf, sizeof(ebuf)));
                    ctx->has_type_error = 1;
                }
            }
        }
        node->type = ctx->current_return_type ? type_deep_copy(ctx->current_return_type) : type_primitive(TYPE_VOID);
        break;
    }

    // ── When guard ────────────────────────────────────────────────────
    case NODE_WHEN: {
        if (node->child_count > 0) {
            AstNode* cond = node->children[0];
            if (cond->type && !type_is_scalar(cond->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, cond->token.loc,
                            "when condition must be scalar");
                ctx->has_type_error = 1;
            }
        }
        break;
    }

    // ── Member access (struct.field) ───────────────────────────────────
    case NODE_MEMBER: {
        if (node->child_count < 1) break;
        // For now, mark as invalid — full struct member type resolution
        // requires the struct field symbol table, which is Phase E work.
        node->type = type_primitive(TYPE_INVALID);
        break;
    }

    // ── Pointer member access (struct->field) ─────────────────────────
    case NODE_DEREF: {
        if (node->child_count < 1) break;
        node->type = type_primitive(TYPE_INVALID);
        break;
    }

    // ── Index (array[i]) ───────────────────────────────────────────────
    case NODE_INDEX: {
        if (node->child_count < 2) break;
        AstNode* arr = node->children[0];
        if (arr->type) {
            if (arr->type->kind == TYPE_ARRAY) {
                node->type = type_deep_copy(arr->type->subtype);
            } else if (arr->type->kind == TYPE_POINTER) {
                node->type = type_deep_copy(arr->type->subtype);
            } else {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "indexing non-array non-pointer type");
                ctx->has_type_error = 1;
                node->type = type_primitive(TYPE_INVALID);
            }
        }
        // Index expression should be integer
        if (node->child_count > 1 && node->children[1]->type) {
            if (!type_is_integer(node->children[1]->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->children[1]->token.loc,
                            "array index must be integer");
                ctx->has_type_error = 1;
            }
        }
        break;
    }

    // ── Cast ───────────────────────────────────────────────────────────
    case NODE_CAST: {
        if (node->child_count < 2) break;
        AstNode* target_type_node = node->children[0];
        Type* cast_type = parse_type_node(target_type_node, ctx);
        if (type_is_error(cast_type)) break;
        node->type = cast_type;
        break;
    }

    // ── Contract nodes ───────────────────────────────────────────────
    case NODE_CONTRACT_PRE: {
        if (node->child_count > 0) {
            AstNode* expr = node->children[0];
            if (expr->type && !type_is_scalar(expr->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, expr->token.loc,
                            "precondition must be scalar");
                ctx->has_type_error = 1;
            }
        }
        break;
    }

    case NODE_CONTRACT_POST: {
        ctx->in_postcondition = 1;
        if (node->child_count > 0) {
            AstNode* expr = node->children[0];
            tc_node(expr, ctx); // Re-check children with postcondition flag
            if (expr->type && !type_is_scalar(expr->type)) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, expr->token.loc,
                            "postcondition must be scalar");
                ctx->has_type_error = 1;
            }
        }
        ctx->in_postcondition = 0;
        break;
    }

    // ── Derivation examples ────────────────────────────────────────────
    case NODE_DERIV_EXAMPLE: {
        // Children: [0..N-2] = inputs, [N-1] = output, optionally tolerance
        // Check that inputs match function params — this is done at function level
        break;
    }

    // ── Function ───────────────────────────────────────────────────────
    case NODE_FUNCTION: {
        // Scan children to find parts
        AstNode* ret_type_node = NULL;
        AstNode* param_list = NULL;
        AstNode* body_node = NULL;
        AstNode* deriv = NULL;
        int has_post = 0; (void)has_post;

        for (size_t i = 0; i < node->child_count; i++) {
            AstNode* child = node->children[i];
            switch (child->kind) {
            case NODE_CONTRACT_PRE:  break;
            case NODE_CONTRACT_POST: has_post = 1; break;
            case NODE_PARAM_LIST:    param_list = child; break;
            case NODE_BLOCK:         body_node = child; break;
            case NODE_DERIVATION:    deriv = child; break;
            default:
                if (!ret_type_node) ret_type_node = child;
                break;
            }
        }

        if (!ret_type_node) break;
        Type* ret_type = parse_type_node(ret_type_node, ctx);
        if (type_is_error(ret_type)) break;

        // Build function type
        size_t param_count = param_list ? param_list->child_count : 0;
        Type** param_types = NULL;
        if (param_count > 0) {
            param_types = (Type**)calloc(param_count, sizeof(Type*));
        }

        // Register params in new scope
        symtab_push_scope(ctx->symtab);

        char fname[256];
        int flen = (int)node->token.len;
        if (flen > 255) flen = 255;
        snprintf(fname, sizeof(fname), "%.*s", flen, node->token.text);

        // Build param types and register each param
        for (size_t i = 0; i < param_count; i++) {
            AstNode* param = param_list->children[i];
            if (param->kind == NODE_DECL && param->child_count >= 1) {
                AstNode* ptype_node = param->children[0];
                Type* ptype = parse_type_node(ptype_node, ctx);
                param_types[i] = ptype;

                // Param name is stored in the node's token, not as a child
                char pname[256];
                int plen = (int)param->token.len;
                if (plen > 255) plen = 255;
                snprintf(pname, sizeof(pname), "%.*s", plen, param->token.text);

                if (param->flags & 1) {
                    if (!type_is_pointer(ptype)) {
                        errlist_add(ctx->errors, ERROR_LEVEL_ERROR, param->token.loc,
                                    "'borrow' modifier on non-pointer type");
                        ctx->has_type_error = 1;
                    }
                }
                if (param->flags & 2) {
                    if (!type_is_pointer(ptype)) {
                        errlist_add(ctx->errors, ERROR_LEVEL_ERROR, param->token.loc,
                                    "'own' modifier on non-pointer type");
                        ctx->has_type_error = 1;
                    }
                }

                symtab_insert(ctx->symtab, pname, ptype, param->token.loc);
            }
        }

        Type* func_type = type_function(ret_type, param_types, param_count);
        node->type = func_type;

        // Register function name in the enclosing scope
        if (ctx->symtab->current && ctx->symtab->current->parent) {
            SymbolTable* st = ctx->symtab;
            Scope* func_scope = st->current;
            st->current = st->current->parent;
            Symbol* existing = symtab_lookup_current(st, fname);
            if (existing) {
                errlist_add(ctx->errors, ERROR_LEVEL_ERROR, node->token.loc,
                            "redeclaration of function '%s'", fname);
                ctx->has_type_error = 1;
            } else {
                symtab_insert(st, fname, func_type, node->token.loc);
            }
            st->current = func_scope;
        }

        // Save return type for body checking — set BEFORE checking body
        Type* saved_ret_type = ctx->current_return_type;
        ctx->current_return_type = ret_type;

        // Re-typecheck the body with params now in scope. The initial recursion
        // processed the body before params were registered, so variable references
        // to parameters failed to resolve.
        if (body_node) {
            clear_type_tree(body_node);
            tc_node(body_node, ctx);
        }

        // Re-check contract expressions with params now in scope
        for (size_t ci = 0; ci < node->child_count; ci++) {
            AstNode* ch = node->children[ci];
            if (ch->kind == NODE_CONTRACT_PRE || ch->kind == NODE_CONTRACT_POST) {
                // The NODE_CONTRACT_POST handler sets in_postcondition AFTER
                // processing children, but we need it set BEFORE so that
                // the `result` variable resolves correctly.
                if (ch->kind == NODE_CONTRACT_POST) {
                    ctx->in_postcondition = 1;
                }
                clear_type_tree(ch);
                tc_node(ch, ctx);
                if (ch->kind == NODE_CONTRACT_POST) {
                    ctx->in_postcondition = 0;
                }
            }
        }

        // Check derivation example inputs vs param count
        if (deriv) {
            for (size_t i = 0; i < deriv->child_count; i++) {
                AstNode* example = deriv->children[i];
                if (example->kind != NODE_DERIV_EXAMPLE) continue;
                size_t example_inputs = example->child_count;
                if (example->child_count > 0) {
                    AstNode* last = example->children[example->child_count - 1];
                    if (last->kind == NODE_DERIV_TOLERANCE) {
                        example_inputs -= 2;
                    } else {
                        example_inputs -= 1;
                    }
                }
                if (example_inputs != param_count) {
                    errlist_add(ctx->errors, ERROR_LEVEL_ERROR, example->token.loc,
                                "derivation example has %zu inputs but function has %zu parameters",
                                example_inputs, param_count);
                    ctx->has_type_error = 1;
                }
            }
        }

        // Restore return type
        ctx->current_return_type = saved_ret_type;

        // Pop the function's scope
        symtab_pop_scope(ctx->symtab);

        if (param_types) {
            for (size_t i = 0; i < param_count; i++) {
                type_free(param_types[i]);
            }
            free(param_types);
        }
        break;
    }

    // ── Struct/Union/Enum declarations ────────────────────────────────
    case NODE_STRUCT_DECL:
    case NODE_UNION_DECL: {
        // Children: [0]=name, [1..N]=fields
        if (node->child_count < 1) break;
        AstNode* name_node = node->children[0];
        char sname[256];
        int snamelen = (int)name_node->token.len;
        if (snamelen > 255) snamelen = 255;
        snprintf(sname, sizeof(sname), "%.*s", snamelen, name_node->token.text);

        TypeKind skind = (node->kind == NODE_STRUCT_DECL) ? TYPE_STRUCT : TYPE_UNION;
        Type* st_type = type_primitive(skind);
        st_type->name = strdup(sname);

        // Register in symbol table
        symtab_insert(ctx->symtab, sname, st_type, node->token.loc);
        break;
    }

    case NODE_ENUM_DECL: {
        if (node->child_count < 1) break;
        AstNode* name_node = node->children[0];
        char ename[256];
        int enamelen = (int)name_node->token.len;
        if (enamelen > 255) enamelen = 255;
        snprintf(ename, sizeof(ename), "%.*s", enamelen, name_node->token.text);

        Type* en_type = type_primitive(TYPE_ENUM);
        en_type->name = strdup(ename);
        symtab_insert(ctx->symtab, ename, en_type, node->token.loc);
        break;
    }

    case NODE_TYPEDEF: {
        // Children: [0]=type_node, [1]=name
        if (node->child_count < 2) break;
        AstNode* underlying_type_node = node->children[0];
        AstNode* alias_name_node = node->children[1];

        Type* underlying = parse_type_node(underlying_type_node, ctx);
        if (type_is_error(underlying)) break;

        char alias[256];
        int aliaslen = (int)alias_name_node->token.len;
        if (aliaslen > 255) aliaslen = 255;
        snprintf(alias, sizeof(alias), "%.*s", aliaslen,
                 alias_name_node->token.text);

        symtab_insert(ctx->symtab, alias, underlying, node->token.loc);
        break;
    }

    // ── Expression statement ──────────────────────────────────────────
    case NODE_EXPR_STMT:
        if (node->child_count > 0 && node->children[0]->type) {
            node->type = type_deep_copy(node->children[0]->type);
        }
        break;

    // ── Nodes that don't produce a type value ──────────────────────────
    case NODE_BLOCK:
    case NODE_WHILE:
    case NODE_DO_WHILE:
    case NODE_FOR:
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_SWITCH:
    case NODE_CASE:
    case NODE_DEFAULT:
    case NODE_IF:
    case NODE_ELSE:
    case NODE_PARAM_LIST:
    case NODE_STRUCT_FIELD:
    case NODE_ARRAY_SUB:
    case NODE_EXTERN:
    case NODE_STATIC_ASSERT:
    case NODE_TRANSLATION_UNIT:
    case NODE_DROP_CALL:
    case NODE_PP_INCLUDE:
    case NODE_PP_DEFINE:
    case NODE_PP_DIRECTIVE:
    case NODE_DERIVATION:
    case NODE_DERIV_TOLERANCE:
    case NODE_BORROW_PARAM:
    case NODE_OWN_PARAM:
    case NODE_NO_DERIVE:
    case NODE_ADDR_OF:
    case NODE_SIZEOF:
        break;

    default:
        break;
    }
}

// ── Public API ───────────────────────────────────────────────────────────

int typecheck_ast(AstNode* root, ErrorList* errors, SymbolTable** out_symtab) {
    if (!root || !errors) return 1;

    TCContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.symtab = symtab_create();
    ctx.errors = errors;
    ctx.current_return_type = NULL;
    ctx.in_postcondition = 0;
    ctx.has_type_error = 0;

    tc_node(root, &ctx);

    if (out_symtab) {
        *out_symtab = ctx.symtab;
    } else {
        symtab_destroy(ctx.symtab);
    }
    return ctx.has_type_error ? 1 : 0;
}

// 2026-07-13 — Value Range Propagation (VRP) implementation.
//   Walks the typed AST and infers integer variable ranges from
//   assignments, for-loop headers, and when-guard conditions.

#include "vrp.h"
#include "type.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

// ── Helpers ──────────────────────────────────────────────────────────────

static int64_t type_max(Type* t) {
    if (!t) return INT64_MAX;
    if (type_is_unsigned(t)) {
        switch (t->bit_width) {
            case 8:  return UINT8_MAX;
            case 16: return UINT16_MAX;
            case 32: return UINT32_MAX;
            case 64: return INT64_MAX; // can't represent UINT64_MAX in int64_t
            default: return INT64_MAX;
        }
    }
    switch (t->bit_width) {
        case 8:  return INT8_MAX;
        case 16: return INT16_MAX;
        case 32: return INT32_MAX;
        case 64: return INT64_MAX;
        default: return INT64_MAX;
    }
}

static int64_t type_min(Type* t) {
    if (!t) return INT64_MIN;
    if (type_is_unsigned(t)) return 0;
    switch (t->bit_width) {
        case 8:  return INT8_MIN;
        case 16: return INT16_MIN;
        case 32: return INT32_MIN;
        case 64: return INT64_MIN;
        default: return INT64_MIN;
    }
}

/// Compute the value range of an expression node.
/// Returns 1 if a range was inferred, 0 otherwise.
static int expr_range(AstNode* node, int64_t* lo, int64_t* hi) {
    if (!node || !node->type) return 0;
    if (!type_is_integer(node->type) && !type_is_bool(node->type)) return 0;

    switch (node->kind) {

    case NODE_LITERAL_INT: {
        *lo = *hi = node->token.value.i64;
        return 1;
    }

    case NODE_VARIABLE: {
        if (node->symbol && node->symbol->range.has_range) {
            *lo = node->symbol->range.lo;
            *hi = node->symbol->range.hi;
            return 1;
        }
        // No range info — use full type range
        *lo = type_min(node->type);
        *hi = type_max(node->type);
        return 1;
    }

    case NODE_UNARY_OP: {
        if (node->child_count < 1) return 0;
        int64_t op_lo, op_hi;
        if (!expr_range(node->children[0], &op_lo, &op_hi)) return 0;

        if (node->token.kind == TOK_MINUS) {
            // -x: swap and negate
            *lo = -op_hi;
            *hi = -op_lo;
            return 1;
        }
        if (node->token.kind == TOK_BIT_NOT) {
            // ~x: approximate as full range
            *lo = type_min(node->type);
            *hi = type_max(node->type);
            return 1;
        }
        return 0;
    }

    case NODE_BINARY_OP: {
        if (node->child_count < 2) return 0;
        int64_t l_lo, l_hi, r_lo, r_hi;
        if (!expr_range(node->children[0], &l_lo, &l_hi)) return 0;
        if (!expr_range(node->children[1], &r_lo, &r_hi)) return 0;

        switch (node->token.kind) {
        case TOK_PLUS:
            *lo = l_lo + r_lo;
            *hi = l_hi + r_hi;
            return 1;
        case TOK_MINUS:
            *lo = l_lo - r_hi;
            *hi = l_hi - r_lo;
            return 1;
        case TOK_STAR: {
            // Approximate: product of extremes
            int64_t candidates[] = {
                l_lo * r_lo, l_lo * r_hi, l_hi * r_lo, l_hi * r_hi
            };
            *lo = candidates[0];
            *hi = candidates[0];
            for (int i = 1; i < 4; i++) {
                if (candidates[i] < *lo) *lo = candidates[i];
                if (candidates[i] > *hi) *hi = candidates[i];
            }
            return 1;
        }
        case TOK_DIV:
            // Division: approximate as full range (complex narrowing)
            *lo = type_min(node->type);
            *hi = type_max(node->type);
            return 1;
        case TOK_EQ: case TOK_NE:
            // Boolean results
            *lo = 0; *hi = 1;
            return 1;
        case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE:
            *lo = 0; *hi = 1;
            return 1;
        case TOK_AND: case TOK_OR:
            *lo = 0; *hi = 1;
            return 1;
        default:
            return 0;
        }
    }

    case NODE_TERNARY: {
        if (node->child_count < 3) return 0;
        int64_t t_lo, t_hi, e_lo, e_hi;
        if (!expr_range(node->children[1], &t_lo, &t_hi)) return 0;
        if (!expr_range(node->children[2], &e_lo, &e_hi)) return 0;
        *lo = t_lo < e_lo ? t_lo : e_lo;
        *hi = t_hi > e_hi ? t_hi : e_hi;
        return 1;
    }

    default:
        return 0;
    }
}

/// Given a comparison `var op bound`, refine var's range.
/// `var_lo`/`var_hi` is the current known range.
/// Returns the refined range via `*out_lo`/`*out_hi`.
static void refine_from_comp(TokenKind op, int64_t bound,
                             int64_t var_lo, int64_t var_hi,
                             int64_t* out_lo, int64_t* out_hi) {
    *out_lo = var_lo;
    *out_hi = var_hi;

    switch (op) {
    case TOK_LT: if (*out_hi >= bound) *out_hi = bound - 1; break;
    case TOK_GT: if (*out_lo <= bound) *out_lo = bound + 1; break;
    case TOK_LE: if (*out_hi >  bound) *out_hi = bound; break;
    case TOK_GE: if (*out_lo <  bound) *out_lo = bound; break;
    case TOK_EQ: *out_lo = *out_hi = bound; break;
    default: break;
    }
}

/// Check if a binary op condition is `var op literal` and return the
/// variable node, operator, and literal value. Returns 1 if matched.
static int match_var_cmp(AstNode* cond, AstNode** var, TokenKind* op, int64_t* val) {
    if (cond->kind != NODE_BINARY_OP || cond->child_count < 2) return 0;
    *op = cond->token.kind;

    // Try: literal op var
    if (cond->children[0]->kind == NODE_LITERAL_INT &&
        cond->children[1]->kind == NODE_VARIABLE) {
        *val = cond->children[0]->token.value.i64;
        *var = cond->children[1];
        // Swap operator for reversed operands
        switch (*op) {
            case TOK_LT: *op = TOK_GT; break;
            case TOK_GT: *op = TOK_LT; break;
            case TOK_LE: *op = TOK_GE; break;
            case TOK_GE: *op = TOK_LE; break;
            default: break;
        }
        return 1;
    }

    // Try: var op literal
    if (cond->children[0]->kind == NODE_VARIABLE &&
        cond->children[1]->kind == NODE_LITERAL_INT) {
        *val = cond->children[1]->token.value.i64;
        *var = cond->children[0];
        return 1;
    }

    return 0;
}

// ── Main VRP walk ────────────────────────────────────────────────────────

static void vrp_walk(AstNode* node, SymbolTable* symtab, ErrorList* errors) {
    if (!node) return;
    (void)errors;

    switch (node->kind) {

    case NODE_VARIABLE: {
        // Copy symbol range to node
        if (node->symbol && node->symbol->range.has_range) {
            node->range = node->symbol->range;
        }
        break;
    }

    case NODE_DECL: {
        if (node->child_count < 2) break;
        // Extract variable name
        char name[256];
        int nlen = (int)node->children[1]->token.len;
        if (nlen > 255) nlen = 255;
        snprintf(name, sizeof(name), "%.*s", nlen, node->children[1]->token.text);

        Symbol* sym = symtab_lookup_current(symtab, name);
        if (!sym) break;

        // Check for initializer (child 2+)
        AstNode* init = NULL;
        for (size_t i = 2; i < node->child_count; i++) {
            if (node->children[i]->kind != NODE_ARRAY_SUB) {
                init = node->children[i];
                break;
            }
        }

        if (init) {
            int64_t lo, hi;
            if (expr_range(init, &lo, &hi)) {
                sym->range.lo = lo;
                sym->range.hi = hi;
                sym->range.has_range = 1;
                node->range = sym->range;
            }
        }
        break;
    }

    case NODE_ASSIGN: {
        if (node->child_count < 2) break;
        AstNode* dest = node->children[0];
        AstNode* src = node->children[1];

        if (dest->kind == NODE_VARIABLE && dest->symbol) {
            int64_t lo, hi;
            if (expr_range(src, &lo, &hi)) {
                dest->symbol->range.lo = lo;
                dest->symbol->range.hi = hi;
                dest->symbol->range.has_range = 1;
                dest->range = dest->symbol->range;
            }
        }
        break;
    }

    case NODE_FOR: {
        // Pattern: for (init; cond; inc) body
        // Track loop variable bounds
        AstNode* init = (node->child_count > 0) ? node->children[0] : NULL;
        AstNode* cond = (node->child_count > 1) ? node->children[1] : NULL;
        AstNode* inc = (node->child_count > 2) ? node->children[2] : NULL;
        AstNode* body = (node->child_count > 3) ? node->children[3] : NULL;

        // Process init (may infer range for loop variable)
        vrp_walk(init, symtab, errors);

        // If cond is `var < N`, infer var ∈ [lo, N-1] in the body
        if (cond && body) {
            AstNode* var = NULL;
            TokenKind op;
            int64_t bound;
            if (match_var_cmp(cond, &var, &op, &bound)) {
                if (var->symbol && (op == TOK_LT || op == TOK_LE)) {
                    int64_t var_lo = var->symbol->range.has_range ? var->symbol->range.lo : type_min(var->type);
                    int64_t var_hi = (op == TOK_LT) ? bound - 1 : bound;
                    if (var_hi < var_lo) var_hi = var_lo;

                    Symbol* loop_sym = var->symbol;
                    ValueRange saved = loop_sym->range;

                    loop_sym->range.lo = var_lo;
                    loop_sym->range.hi = var_hi;
                    loop_sym->range.has_range = 1;

                    vrp_walk(body, symtab, errors);

                    loop_sym->range = saved;
                    break; // skip normal child walk
                }
            }
        }

        // Fallthrough: walk normally
        vrp_walk(body, symtab, errors);
        vrp_walk(inc, symtab, errors);
        break;
    }

    case NODE_WHEN: {
        if (node->child_count < 2) break;
        AstNode* cond = node->children[0];
        AstNode* stmt = node->children[1];

        // Try to refine range from condition
        AstNode* var = NULL;
        TokenKind op;
        int64_t bound;
        if (match_var_cmp(cond, &var, &op, &bound) && var->symbol) {
            int64_t var_lo = var->symbol->range.has_range ? var->symbol->range.lo : type_min(var->type);
            int64_t var_hi = var->symbol->range.has_range ? var->symbol->range.hi : type_max(var->type);

            int64_t refined_lo, refined_hi;
            refine_from_comp(op, bound, var_lo, var_hi, &refined_lo, &refined_hi);

            Symbol* when_sym = var->symbol;
            ValueRange saved = when_sym->range;

            when_sym->range.lo = refined_lo;
            when_sym->range.hi = refined_hi;
            when_sym->range.has_range = 1;

            vrp_walk(stmt, symtab, errors);

            when_sym->range = saved;
        } else {
            vrp_walk(stmt, symtab, errors);
        }
        break;
    }

    default:
        // Walk children
        for (size_t i = 0; i < node->child_count; i++) {
            vrp_walk(node->children[i], symtab, errors);
        }
        break;
    }
}

// ── Public API ───────────────────────────────────────────────────────────

int vrp_run(AstNode* root, SymbolTable* symtab, ErrorList* errors) {
    if (!root || !symtab) return -1;
    vrp_walk(root, symtab, errors);
    return 0;
}

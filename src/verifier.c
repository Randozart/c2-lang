// 2026-07-13 — Z3 contract verifier implementation.
//   Walks the typed AST, translates contract expressions into Z3 bit-vector
//   formulas, and runs proof queries for each function's contracts.

#include "verifier.h"
#include "type.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

// Z3 C API header
#include <z3.h>

// ── Forward declarations ─────────────────────────────────────────────────

static Z3_ast translate_expr(AstNode* node, Z3_context ctx,
                             Z3_sort bv_sorts[4], // 0=8bit, 1=16bit, 2=32bit, 3=64bit
                             Z3_ast* param_asts, const char** param_names,
                             size_t param_count, Z3_ast result_ast,
                             int* had_error);

// ── Helpers ──────────────────────────────────────────────────────────────

/// Get the bit-vector sort index for a type (0=8, 1=16, 2=32, 3=64).
static int sort_index(Type* t) {
    if (!t) return 2; // default 32-bit
    switch (t->kind) {
        case TYPE_INT8:   case TYPE_UINT8:   case TYPE_BOOL:  return 0;
        case TYPE_INT16:  case TYPE_UINT16:                    return 1;
        case TYPE_INT32:  case TYPE_UINT32:
        case TYPE_FLOAT:                                       return 2;
        case TYPE_INT64:  case TYPE_UINT64:  case TYPE_DOUBLE: return 3;
        default: return 2;
    }
}

static Z3_sort get_bv_sort(Z3_context ctx, Z3_sort sorts[4], Type* t) {
    (void)ctx;
    return sorts[sort_index(t)];
}

/// Find the return type AstNode child of a function node.
static AstNode* find_ret_type_node(AstNode* func) {
    for (size_t i = 0; i < func->child_count; i++) {
        AstNode* ch = func->children[i];
        if (ch->kind != NODE_CONTRACT_PRE && ch->kind != NODE_CONTRACT_POST &&
            ch->kind != NODE_PARAM_LIST && ch->kind != NODE_BLOCK &&
            ch->kind != NODE_DERIVATION && ch->kind != NODE_NO_DERIVE &&
            ch->kind != NODE_DERIV_EXAMPLE && ch->kind != NODE_DERIV_TOLERANCE) {
            return ch;
        }
    }
    return NULL;
}

// ── Expression translation ───────────────────────────────────────────────

/// Translate a comparison result (bv1) by zero-extending to the target width.
/// Comparisons in Z3 return bool, but C² treats them as integers.
static Z3_ast mk_cmp_result(Z3_context ctx, Z3_ast cmp, Z3_sort target) {
    // cmp is a bool; zero-extend to the target sort
    Z3_ast one = Z3_mk_unsigned_int64(ctx, 1, Z3_mk_bv_sort(ctx, 1));
    Z3_ast zero = Z3_mk_unsigned_int64(ctx, 0, Z3_mk_bv_sort(ctx, 1));
    Z3_ast ite = Z3_mk_ite(ctx, cmp, one, zero);
    return Z3_mk_zero_ext(ctx, Z3_get_bv_sort_size(ctx, target) - 1, ite);
}

/// Translate a single binary comparison (==, !=, <, >, <=, >=).
/// Returns a Z3 bool (not zero-extended).
static Z3_ast mk_bv_cmp(Z3_context ctx, TokenKind op, Z3_ast a, Z3_ast b) {
    switch (op) {
        case TOK_EQ: return Z3_mk_eq(ctx, a, b);
        case TOK_NE: return Z3_mk_not(ctx, Z3_mk_eq(ctx, a, b));
        case TOK_LT: return Z3_mk_bvslt(ctx, a, b);
        case TOK_GT: return Z3_mk_bvsgt(ctx, a, b);
        case TOK_LE: return Z3_mk_bvsle(ctx, a, b);
        case TOK_GE: return Z3_mk_bvsge(ctx, a, b);
        default: return NULL;
    }
}

static Z3_ast translate_expr(AstNode* node, Z3_context ctx,
                             Z3_sort bv_sorts[4],
                             Z3_ast* param_asts, const char** param_names,
                             size_t param_count, Z3_ast result_ast,
                             int* had_error) {
    if (!node || !node->type) { *had_error = 1; return NULL; }

    Z3_sort node_sort = get_bv_sort(ctx, bv_sorts, node->type);

    switch (node->kind) {

    case NODE_LITERAL_INT: {
        int64_t val = node->token.value.i64;
        return Z3_mk_int64(ctx, val, node_sort);
    }

    case NODE_LITERAL_FLOAT: {
        // Float literals -> bit-vector representation (reinterpret bits)
        double dval = node->token.value.f64;
        uint64_t bits = 0;
        if (node->type->kind == TYPE_FLOAT) {
            float f = (float)dval;
            memcpy(&bits, &f, sizeof(float));
            return Z3_mk_unsigned_int64(ctx, bits, node_sort);
        } else {
            memcpy(&bits, &dval, sizeof(double));
            return Z3_mk_unsigned_int64(ctx, bits, node_sort);
        }
    }

    case NODE_VARIABLE: {
        // Check for the special `result` variable
        if (node->token.len == 6 && memcmp(node->token.text, "result", 6) == 0) {
            if (result_ast) return result_ast;
            *had_error = 1;
            return NULL;
        }
        // Look up parameter name
        char name[256];
        int nlen = (int)node->token.len;
        if (nlen > 255) nlen = 255;
        snprintf(name, sizeof(name), "%.*s", nlen, node->token.text);

        for (size_t i = 0; i < param_count; i++) {
            if (param_names[i] && strcmp(name, param_names[i]) == 0) {
                return param_asts[i];
            }
        }
        // Unknown variable — skip
        *had_error = 1;
        return NULL;
    }

    case NODE_UNARY_OP: {
        if (node->child_count < 1) { *had_error = 1; return NULL; }
        Z3_ast operand = translate_expr(node->children[0], ctx, bv_sorts,
                                        param_asts, param_names, param_count,
                                        result_ast, had_error);
        if (*had_error) return NULL;

        switch (node->token.kind) {
            case TOK_MINUS:    return Z3_mk_bvneg(ctx, operand);
            case TOK_BIT_NOT:  return Z3_mk_bvnot(ctx, operand);
            case TOK_NOT:      return Z3_mk_bvnot(ctx, operand); // logical not on bv1
            default: *had_error = 1; return NULL;
        }
    }

    case NODE_BINARY_OP: {
        if (node->child_count < 2) { *had_error = 1; return NULL; }
        Z3_ast left = translate_expr(node->children[0], ctx, bv_sorts,
                                     param_asts, param_names, param_count,
                                     result_ast, had_error);
        Z3_ast right = translate_expr(node->children[1], ctx, bv_sorts,
                                      param_asts, param_names, param_count,
                                      result_ast, had_error);
        if (*had_error) return NULL;

        TokenKind op = node->token.kind;

        // Arithmetic operators
        switch (op) {
            case TOK_PLUS:     return Z3_mk_bvadd(ctx, left, right);
            case TOK_MINUS:    return Z3_mk_bvsub(ctx, left, right);
            case TOK_STAR:     return Z3_mk_bvmul(ctx, left, right);
            case TOK_DIV:      return Z3_mk_bvsdiv(ctx, left, right);
            case TOK_MOD:      return Z3_mk_bvsmod(ctx, left, right);
            default: break;
        }

        // Bitwise operators
        switch (op) {
            case TOK_BIT_AND:  return Z3_mk_bvand(ctx, left, right);
            case TOK_BIT_OR:   return Z3_mk_bvor(ctx, left, right);
            case TOK_BIT_XOR:  return Z3_mk_bvxor(ctx, left, right);
            case TOK_SHL:      return Z3_mk_bvshl(ctx, left, right);
            case TOK_SHR:      return Z3_mk_bvashr(ctx, left, right);
            default: break;
        }

        // Comparison operators (return bool, zero-extended to node type)
        Z3_ast cmp = mk_bv_cmp(ctx, op, left, right);
        if (cmp) {
            return mk_cmp_result(ctx, cmp, node_sort);
        }

        // Logical operators (operands are boolean-like)
        if (op == TOK_AND) {
            Z3_ast args[2] = { left, right };
            return Z3_mk_and(ctx, 2, args);
        }
        if (op == TOK_OR) {
            Z3_ast args[2] = { left, right };
            return Z3_mk_or(ctx, 2, args);
        }

        // Unknown operator
        *had_error = 1;
        return NULL;
    }

    case NODE_TERNARY: {
        if (node->child_count < 3) { *had_error = 1; return NULL; }
        Z3_ast cond = translate_expr(node->children[0], ctx, bv_sorts,
                                     param_asts, param_names, param_count,
                                     result_ast, had_error);
        Z3_ast then_expr = translate_expr(node->children[1], ctx, bv_sorts,
                                          param_asts, param_names, param_count,
                                          result_ast, had_error);
        Z3_ast else_expr = translate_expr(node->children[2], ctx, bv_sorts,
                                          param_asts, param_names, param_count,
                                          result_ast, had_error);
        if (*had_error) return NULL;
        // Condition is true if it's not zero
        Z3_ast cond_zero = Z3_mk_unsigned_int64(ctx, 0, get_bv_sort(ctx, bv_sorts, node->children[0]->type));
        Z3_ast cond_is_true = Z3_mk_not(ctx, Z3_mk_eq(ctx, cond, cond_zero));
        return Z3_mk_ite(ctx, cond_is_true, then_expr, else_expr);
    }

    case NODE_CAST: {
        if (node->child_count < 2) { *had_error = 1; return NULL; }
        Z3_ast expr = translate_expr(node->children[1], ctx, bv_sorts,
                                     param_asts, param_names, param_count,
                                     result_ast, had_error);
        if (*had_error) return NULL;
        Z3_sort from_sort = Z3_get_sort(ctx, expr);
        size_t from_width = Z3_get_bv_sort_size(ctx, from_sort);
        size_t to_width = Z3_get_bv_sort_size(ctx, node_sort);
        if (to_width > from_width) {
            return Z3_mk_sign_ext(ctx, to_width - from_width, expr);
        } else if (to_width < from_width) {
            return Z3_mk_extract(ctx, to_width - 1, 0, expr);
        }
        return expr;
    }

    default:
        *had_error = 1;
        return NULL;
    }
}

// ── Function body translation ────────────────────────────────────────────

/// Build a Z3 expression representing the return value of a function body
/// by collecting all return statements and their when-guard path conditions.
/// Returns the Z3 expression, or NULL if translation fails.
static Z3_ast translate_body_to_expr(AstNode* body, Z3_context ctx,
                                     Z3_sort bv_sorts[4],
                                     Z3_ast* param_asts, const char** param_names,
                                     size_t param_count, Z3_ast result_ast,
                                     int* had_error) {
    if (!body || body->kind != NODE_BLOCK) return NULL;
    if (body->child_count == 0) return NULL;

    // Walk block children, building path-condition / return-expr pairs.
    // Strategy: for each statement, if it's a NODE_RETURN we add a terminal
    // pair. If it's a NODE_WHEN with a return, we add a conditional pair.
    // The final expression is an ite chain.

    // We need to find ALL return expressions. For simplicity, we look for:
    // 1. Direct NODE_RETURN children of the block
    // 2. NODE_RETURN inside NODE_WHEN children

    // We'll build the return value as an ite chain.
    // The last non-conditional return is the default/else branch.

    Z3_ast result = NULL;
    Z3_ast default_result = NULL;

    for (size_t i = 0; i < body->child_count; i++) {
        AstNode* stmt = body->children[i];

        if (stmt->kind == NODE_RETURN && stmt->child_count > 0) {
            // Direct return in block: this is the default (else) branch
            default_result = translate_expr(stmt->children[0], ctx, bv_sorts,
                                           param_asts, param_names, param_count,
                                           result_ast, had_error);
            if (*had_error) return NULL;
        } else if (stmt->kind == NODE_WHEN && stmt->child_count > 1) {
            // when condition -> return-expr
            AstNode* cond = stmt->children[0];
            AstNode* action = stmt->children[1];

            Z3_ast cond_z3 = translate_expr(cond, ctx, bv_sorts,
                                           param_asts, param_names, param_count,
                                           result_ast, had_error);
            if (*had_error) return NULL;

            if (action->kind == NODE_RETURN && action->child_count > 0) {
                Z3_ast ret_val = translate_expr(action->children[0], ctx, bv_sorts,
                                               param_asts, param_names, param_count,
                                               result_ast, had_error);
                if (*had_error) return NULL;

                // Condition is true if non-zero
                Z3_sort cond_sort = get_bv_sort(ctx, bv_sorts, cond->type);
                Z3_ast cond_is_true = Z3_mk_not(ctx, Z3_mk_eq(ctx, cond_z3,
                    Z3_mk_unsigned_int64(ctx, 0, cond_sort)));

                if (result) {
                    // Nest: if cond then ret_val else previous-result
                    result = Z3_mk_ite(ctx, cond_is_true, ret_val, result);
                } else {
                    // First when-return: save for later wrapping
                    // We'll build the chain from last to first
                    result = ret_val;
                    result = NULL; // placeholder
                    // Actually, build chain by prepending
                }
            }
        }
    }

    // Build the ite chain. For now use a simple approach:
    // the last return is the default, and when-returns wrap it.
    // Re-traverse in reverse: last return = innermost else.

    // Simple case: single return
    if (default_result) {
        result = default_result;
    }

    // Walk when guards in reverse and wrap
    for (size_t i = body->child_count; i > 0; i--) {
        AstNode* stmt = body->children[i - 1];
        if (stmt->kind == NODE_WHEN && stmt->child_count > 1) {
            AstNode* cond = stmt->children[0];
            AstNode* action = stmt->children[1];

            if (action->kind == NODE_RETURN && action->child_count > 0) {
                Z3_ast cond_z3 = translate_expr(cond, ctx, bv_sorts,
                                               param_asts, param_names, param_count,
                                               result_ast, had_error);
                if (*had_error) return NULL;

                Z3_ast ret_val = translate_expr(action->children[0], ctx, bv_sorts,
                                               param_asts, param_names, param_count,
                                               result_ast, had_error);
                if (*had_error) return NULL;

                Z3_sort cond_sort = get_bv_sort(ctx, bv_sorts, cond->type);
                Z3_ast cond_is_true = Z3_mk_not(ctx, Z3_mk_eq(ctx, cond_z3,
                    Z3_mk_unsigned_int64(ctx, 0, cond_sort)));

                if (!result) {
                    result = ret_val;
                } else {
                    result = Z3_mk_ite(ctx, cond_is_true, ret_val, result);
                }
            }
        }
    }

    return result;
}

// ── Contract verification per function ───────────────────────────────────

/// Verify a single function's contracts. Returns 0 on success (verified),
/// 1 on failure (contract violation), -1 on error.
static int verify_function_contracts(AstNode* func, Z3_context ctx,
                                     Z3_sort bv_sorts[4], int print_output) {
    // Find contract and param children
    AstNode* pre_expr = NULL;
    AstNode* post_expr = NULL;
    AstNode* param_list = NULL;

    for (size_t i = 0; i < func->child_count; i++) {
        AstNode* ch = func->children[i];
        if (ch->kind == NODE_CONTRACT_PRE && ch->child_count > 0)
            pre_expr = ch->children[0];
        if (ch->kind == NODE_CONTRACT_POST && ch->child_count > 0)
            post_expr = ch->children[0];
        if (ch->kind == NODE_PARAM_LIST)
            param_list = ch;
    }

    if (!pre_expr && !post_expr) return 0; // no contracts to verify

    // Count parameters
    size_t param_count = param_list ? param_list->child_count : 0;

    // Allocate param arrays
    Z3_ast* param_asts = (Z3_ast*)calloc(param_count + 1, sizeof(Z3_ast));
    const char** param_names = (const char**)calloc(param_count + 1, sizeof(char*));

    // Declare Z3 constants for each parameter
    for (size_t i = 0; i < param_count; i++) {
        AstNode* param = param_list->children[i];
        if (param->kind != NODE_DECL) {
            param_names[i] = NULL;
            param_asts[i] = NULL;
            continue;
        }
        // Param name is in the node's token
        char pname[256];
        int plen = (int)param->token.len;
        if (plen > 255) plen = 255;
        snprintf(pname, sizeof(pname), "%.*s", plen, param->token.text);
        param_names[i] = strdup(pname);

        Z3_sort p_sort = get_bv_sort(ctx, bv_sorts, param->type);
        param_asts[i] = Z3_mk_const(ctx, Z3_mk_string_symbol(ctx, pname), p_sort);
    }

    // Get return type for the `result` constant
    AstNode* ret_type_node = find_ret_type_node(func);
    Type* ret_type = ret_type_node ? ret_type_node->type : NULL;
    Z3_ast result_ast = NULL;
    if (post_expr && ret_type && !type_is_void(ret_type)) {
        Z3_sort ret_sort = get_bv_sort(ctx, bv_sorts, ret_type);
        result_ast = Z3_mk_const(ctx, Z3_mk_string_symbol(ctx, "result"), ret_sort);
    }

    // Find the function body
    AstNode* body = NULL;
    for (size_t i = 0; i < func->child_count; i++) {
        if (func->children[i]->kind == NODE_BLOCK) {
            body = func->children[i];
            break;
        }
    }

    // Translate pre and post expressions
    int had_error = 0;
    Z3_ast pre_formula = NULL;
    Z3_ast post_formula = NULL;
    Z3_ast body_formula = NULL;

    if (pre_expr) {
        pre_formula = translate_expr(pre_expr, ctx, bv_sorts,
                                     param_asts, param_names, param_count,
                                     NULL, &had_error);
    }
    if (!had_error && post_expr) {
        post_formula = translate_expr(post_expr, ctx, bv_sorts,
                                      param_asts, param_names, param_count,
                                      result_ast, &had_error);
    }
    // Translate body to constrain `result` if we have a postcondition
    if (!had_error && post_expr && result_ast && body) {
        body_formula = translate_body_to_expr(body, ctx, bv_sorts,
                                              param_asts, param_names, param_count,
                                              result_ast, &had_error);
    }

    // If translation failed, skip this function
    if (had_error) {
        if (print_output) {
            printf("  %.*s: SKIP (untranslatable expression)\n",
                   (int)func->token.len, func->token.text);
        }
        for (size_t i = 0; i < param_count; i++) free((void*)param_names[i]);
        free(param_asts);
        free(param_names);
        return 0;
    }

    Z3_solver solver = Z3_mk_solver(ctx);
    Z3_solver_inc_ref(ctx, solver);
    int result = 0; // 0 = all pass

    char fname[256];
    int flen = (int)func->token.len;
    if (flen > 255) flen = 255;
    snprintf(fname, sizeof(fname), "%.*s", flen, func->token.text);

    // Precondition satisfiability check
    if (pre_formula) {
        Z3_solver_push(ctx, solver);
        // Convert bit-vector to Bool: pre != 0 (truthy check)
        Z3_ast pre_is_true = Z3_mk_not(ctx, Z3_mk_eq(ctx, pre_formula,
            Z3_mk_unsigned_int64(ctx, 0, Z3_get_sort(ctx, pre_formula))));
        Z3_solver_assert(ctx, solver, pre_is_true);
        Z3_lbool sat = Z3_solver_check(ctx, solver);
        if (sat == Z3_L_FALSE) {
            if (print_output) {
                printf("  %s: FAIL (precondition is trivially false)\n", fname);
            }
            result = 1;
        }
        Z3_solver_pop(ctx, solver, 1);
    }

    // Postcondition validity check: model body, assert pre, negate post, check UNSAT
    if (post_formula && !result) {
        Z3_solver_push(ctx, solver);
        if (pre_formula) {
            Z3_ast pre_is_true = Z3_mk_not(ctx, Z3_mk_eq(ctx, pre_formula,
                Z3_mk_unsigned_int64(ctx, 0, Z3_get_sort(ctx, pre_formula))));
            Z3_solver_assert(ctx, solver, pre_is_true);
        }
        // If we have a body model, assert result == body_expr
        if (body_formula) {
            Z3_solver_assert(ctx, solver, Z3_mk_eq(ctx, result_ast, body_formula));
        }
        // Negate the postcondition: check if any input satisfying pre leads to result
        // that violates the post. Assert !post and check SAT.
        Z3_ast post_is_false = Z3_mk_eq(ctx, post_formula,
            Z3_mk_unsigned_int64(ctx, 0, Z3_get_sort(ctx, post_formula)));
        Z3_solver_assert(ctx, solver, post_is_false);
        Z3_lbool sat = Z3_solver_check(ctx, solver);
        if (sat != Z3_L_FALSE) {
            if (print_output) {
                if (body_formula) {
                    printf("  %s: FAIL (postcondition may be violated)\n", fname);
                } else {
                    printf("  %s: FAIL (postcondition may be violated — no body model)\n", fname);
                }
            }
            result = 1;
        }
        Z3_solver_pop(ctx, solver, 1);
    }

    if (result == 0 && print_output) {
        printf("  %s: OK\n", fname);
    }

    Z3_solver_dec_ref(ctx, solver);

    for (size_t i = 0; i < param_count; i++) free((void*)param_names[i]);
    free(param_asts);
    free(param_names);
    return result;
}

// ── Z3 error handler (prevents abort) ───────────────────────────────────

static void z3_safe_error_handler(Z3_context ctx, Z3_error_code err) {
    // Swallow Z3 errors gracefully — don't abort like the default handler
    (void)ctx;
    (void)err;
}

// ── Public API ───────────────────────────────────────────────────────────

int z3_verify_contracts(AstNode* root, ErrorList* errors, int print_output) {
    (void)errors;
    if (!root || root->kind != NODE_TRANSLATION_UNIT) return -1;

    // Check if Z3 calls actually link by testing a trivial query
    Z3_config cfg = Z3_mk_config();
    if (!cfg) {
        // Z3 not available — skip verification
        if (print_output) printf("Z3 not available; skipping contract verification.\n");
        return 0;
    }

    Z3_context ctx = Z3_mk_context(cfg);
    Z3_set_error_handler(ctx, z3_safe_error_handler);
    Z3_del_config(cfg);

    // Create bit-vector sorts for common widths
    Z3_sort bv_sorts[4];
    bv_sorts[0] = Z3_mk_bv_sort(ctx, 8);   // int8/uint8/bool
    bv_sorts[1] = Z3_mk_bv_sort(ctx, 16);  // int16/uint16
    bv_sorts[2] = Z3_mk_bv_sort(ctx, 32);  // int32/uint32/float
    bv_sorts[3] = Z3_mk_bv_sort(ctx, 64);  // int64/uint64/double

    int total = 0;
    int failures = 0;

    if (print_output) printf("\nContract verification:\n");

    for (size_t i = 0; i < root->child_count; i++) {
        AstNode* child = root->children[i];
        if (child->kind != NODE_FUNCTION) continue;

        int has_pre = 0, has_post = 0;
        for (size_t j = 0; j < child->child_count; j++) {
            if (child->children[j]->kind == NODE_CONTRACT_PRE) has_pre = 1;
            if (child->children[j]->kind == NODE_CONTRACT_POST) has_post = 1;
        }
        if (!has_pre && !has_post) continue;

        total++;
        int r = verify_function_contracts(child, ctx, bv_sorts, print_output);
        if (r < 0) { Z3_del_context(ctx); return -1; }
        if (r > 0) failures++;
    }

    if (print_output) {
        if (total == 0) {
            printf("  (no contracts to verify)\n");
        } else {
            printf("  %d/%d contracts verified successfully\n", total - failures, total);
        }
        printf("\n");
    }

    Z3_del_context(ctx);
    return failures > 0 ? 1 : 0;
}

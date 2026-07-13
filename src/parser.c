// 2026-07-13 — Recursive-descent parser for C².
//   Builds an AST from the token stream produced by the lexer.
//   Supports C23 syntax plus C² extensions: contracts, guards,
//   derivation blocks, and ownership modifiers.

#include "parser.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ── Forward declarations ────────────────────────────────────────────────

static AstNode* parse_declaration(Parser* p);
static AstNode* parse_struct_decl(Parser* p);
static AstNode* parse_union_decl(Parser* p);
static AstNode* parse_enum_decl(Parser* p);
static AstNode* parse_typedef(Parser* p);
static AstNode* parse_contract(Parser* p, AstNode** pre, AstNode** post);
static AstNode* parse_parameter_list(Parser* p);
static AstNode* parse_parameter(Parser* p);
static AstNode* parse_derivation_block(Parser* p);
static AstNode* parse_block(Parser* p);
static AstNode* parse_statement(Parser* p);
static AstNode* parse_expr(Parser* p);
static AstNode* parse_assignment(Parser* p);
static AstNode* parse_conditional(Parser* p);
static AstNode* parse_logical_or(Parser* p);
static AstNode* parse_logical_and(Parser* p);
static AstNode* parse_bit_or(Parser* p);
static AstNode* parse_bit_xor(Parser* p);
static AstNode* parse_bit_and(Parser* p);
static AstNode* parse_equality(Parser* p);
static AstNode* parse_relational(Parser* p);
static AstNode* parse_shift(Parser* p);
static AstNode* parse_term(Parser* p);
static AstNode* parse_factor(Parser* p);
static AstNode* parse_unary(Parser* p);
static AstNode* parse_postfix(Parser* p);
static AstNode* parse_primary(Parser* p);
static AstNode* parse_type(Parser* p);
static int is_type_token(TokenKind kind);
static Token consume(Parser* p);
static Token expect(Parser* p, TokenKind kind);
static int match(Parser* p, TokenKind kind);
static int check(Parser* p, TokenKind kind);
static Token peek(Parser* p);
static void synchronize(Parser* p);

// ── Parser creation ─────────────────────────────────────────────────────

Parser parser_create(const char* source, size_t source_len, const char* filename, ErrorList* errors) {
    Parser p;
    p.lexer = lexer_create(source, source_len, filename, errors);
    p.errors = errors;
    p.current_function = NULL;
    // Prime the first token
    lexer_scan(&p.lexer);
    return p;
}

// ── Top-level entry point ───────────────────────────────────────────────

AstNode* parser_parse(Parser* p) {
    AstNode* root = ast_alloc_node(NODE_TRANSLATION_UNIT, peek(p));
    size_t iter = 0;
    while (!check(p, TOK_EOF)) {
        iter++;
        if (iter > 100) {
            fprintf(stderr, "parser: too many iterations, aborting\n");
            break;
        }
        AstNode* decl = parse_declaration(p);
        if (decl) {
            ast_add_child(root, decl);
        } else {
            synchronize(p);
        }
    }
    return root;
}

// ── Declarations ────────────────────────────────────────────────────────

static AstNode* parse_declaration(Parser* p) {
    // Handle preprocessor directives (#include, #define, etc.)
    if (check(p, TOK_PP_INCLUDE) || check(p, TOK_PP_DEFINE) ||
        check(p, TOK_PP_DIRECTIVE) || check(p, TOK_PP_IF) ||
        check(p, TOK_PP_IFDEF) || check(p, TOK_PP_IFNDEF) ||
        check(p, TOK_PP_ELSE) || check(p, TOK_PP_ENDIF) ||
        check(p, TOK_PP_LINE) || check(p, TOK_PP_PRAGMA)) {
        Token t = consume(p);
        return ast_alloc_node(NODE_PP_INCLUDE, t);
    }

    // Handle typedef
    if (check(p, TOK_TYPEDEF)) return parse_typedef(p);

    // Handle struct/union/enum: definitions, forward declarations, and type references
    // We peek ahead to distinguish: `struct name { ... }` vs `struct name;` vs `struct name var;`
    if (check(p, TOK_STRUCT) || check(p, TOK_UNION) || check(p, TOK_ENUM)) {
        TokenKind kw = p->lexer.current_tok.kind;
        Token after = peek(p);

        if (after.kind == TOK_LBRACE) {
            // Anonymous definition: `struct { ... }`
            AstNode* node = (kw == TOK_STRUCT) ? parse_struct_decl(p)
                         : (kw == TOK_UNION)  ? parse_union_decl(p)
                         :                       parse_enum_decl(p);
            if (check(p, TOK_IDENTIFIER)) {
                Token var_name = consume(p);
                AstNode* var_decl = ast_alloc_node(NODE_DECL, var_name);
                ast_add_child(var_decl, node);
                expect(p, TOK_SEMI);
                AstNode* stmt = ast_alloc_node(NODE_EXPR_STMT, var_decl->token);
                ast_add_child(stmt, var_decl);
                return stmt;
            }
            return node;
        }

        if (after.kind == TOK_IDENTIFIER) {
            // Peek one more token to decide
            Lexer* l = &p->lexer;
            const char* saved_c = l->current;
            size_t saved_o = l->offset;
            size_t saved_line = l->line;
            size_t saved_col = l->col;
            const char* saved_s = l->start;
            Token saved_ct = l->current_tok;
            Token saved_pv = l->prev;
            int saved_hp = l->has_prev;
            lexer_scan(l); lexer_scan(l);
            TokenKind after_two = l->current_tok.kind;
            l->current = saved_c; l->offset = saved_o; l->line = saved_line;
            l->col = saved_col; l->start = saved_s;
            l->current_tok = saved_ct; l->prev = saved_pv; l->has_prev = saved_hp;

            if (after_two == TOK_LBRACE) {
                // Definition: `struct name { ... }`
                AstNode* node = (kw == TOK_STRUCT) ? parse_struct_decl(p)
                             : (kw == TOK_UNION)  ? parse_union_decl(p)
                             :                       parse_enum_decl(p);
                if (check(p, TOK_IDENTIFIER)) {
                    Token var_name = consume(p);
                    AstNode* var_decl = ast_alloc_node(NODE_DECL, var_name);
                    ast_add_child(var_decl, node);
                    if (kw != TOK_ENUM && match(p, TOK_LBRACK)) {
                        AstNode* size = parse_expr(p);
                        AstNode* sub = ast_alloc_node(NODE_ARRAY_SUB, size ? size->token : peek(p));
                        if (size) ast_add_child(sub, size);
                        ast_add_child(var_decl, sub);
                        expect(p, TOK_RBRACK);
                    }
                    expect(p, TOK_SEMI);
                    AstNode* stmt = ast_alloc_node(NODE_EXPR_STMT, var_decl->token);
                    ast_add_child(stmt, var_decl);
                    return stmt;
                }
                return node;
            }

            if (after_two == TOK_SEMI) {
                // Forward declaration: `struct name;`
                consume(p); // struct
                Token name_tok = consume(p); // name
                match(p, TOK_SEMI);
                AstNode* node = ast_alloc_node(
                    kw == TOK_STRUCT ? NODE_STRUCT_DECL :
                    kw == TOK_UNION  ? NODE_UNION_DECL :
                                       NODE_ENUM_DECL,
                    name_tok);
                AstNode* name_node = ast_alloc_node(NODE_VARIABLE, name_tok);
                ast_add_child(node, name_node);
                return node;
            }

            // Type reference: `struct name var;` — fall through to generic path
            goto generic_decl;
        }

        // Single `struct;` or `struct name` without any following token — error or fall through
        // Let generic path handle it with parse_type
        goto generic_decl;
    }

generic_decl:
    // Check for no_derive pragma
    int has_no_derive = 0;
    if (match(p, TOK_NO_DERIVE)) {
        has_no_derive = 1;
    }

    // Check for a contract before a function definition
    AstNode* pre = NULL;
    AstNode* post = NULL;
    if (check(p, TOK_LBRACK) || check(p, TOK_DBL_OPEN)) {
        AstNode* contract_node = parse_contract(p, &pre, &post);
        (void)contract_node;
    }

    // Parse a type (simplified)
    AstNode* type = parse_type(p);
    if (!type) {
        if (!check(p, TOK_EOF)) {
            errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
                        "expected type declaration");
        }
        return NULL;
    }

    // Next must be an identifier (function or variable name)
    if (!check(p, TOK_IDENTIFIER)) {
        errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc, "expected identifier");
        return NULL;
    }
    Token name_tok = consume(p);

    // If next is '(', it's a function definition
    if (match(p, TOK_LPAREN)) {
        AstNode* params = parse_parameter_list(p);
        expect(p, TOK_RPAREN);

        // Check for derivation block := { ... }
        if (match(p, TOK_DERIVE)) {
            AstNode* deriv = parse_derivation_block(p);
            AstNode* func = ast_make_function(name_tok, type, params, NULL, pre, post, deriv);
            if (has_no_derive) {
                AstNode* nd = ast_alloc_node(NODE_NO_DERIVE, name_tok);
                ast_add_child(func, nd);
            }
            return func;
        }

        // Otherwise, parse body
        expect(p, TOK_LBRACE);
        AstNode* body = parse_block(p);

        // Check for optional derivation block after body
        AstNode* deriv = NULL;
        if (match(p, TOK_DERIVE)) {
            deriv = parse_derivation_block(p);
        }

        // Enforce contracts: every function must have at least a pre, post,
        // or derivation block (unless marked no_derive).
        if (pre == NULL && post == NULL && deriv == NULL && !has_no_derive) {
            errlist_add(p->errors, ERROR_LEVEL_ERROR, name_tok.loc,
                        "function '%.*s' has no contract — declare [pre][post], [[post]], or [pre]]",
                        (int)name_tok.len, name_tok.text);
        }

        AstNode* func = ast_make_function(name_tok, type, params, body, pre, post, deriv);
        if (has_no_derive) {
            AstNode* nd = ast_alloc_node(NODE_NO_DERIVE, name_tok);
            ast_add_child(func, nd);
        }
        return func;
    }

    // Global variable declaration: parse optional array subscript and initializer
    AstNode* decl = ast_alloc_node(NODE_DECL, name_tok);
    ast_add_child(decl, type);

    if (match(p, TOK_LBRACK)) {
        AstNode* size = parse_expr(p);
        AstNode* sub = ast_alloc_node(NODE_ARRAY_SUB, size ? size->token : peek(p));
        if (size) ast_add_child(sub, size);
        ast_add_child(decl, sub);
        expect(p, TOK_RBRACK);
    }

    if (match(p, TOK_ASSIGN)) {
        AstNode* init = parse_expr(p);
        if (init) ast_add_child(decl, init);
    }

    expect(p, TOK_SEMI);

    AstNode* stmt = ast_alloc_node(NODE_EXPR_STMT, decl->token);
    ast_add_child(stmt, decl);
    return stmt;
}

// ── Contract parsing ────────────────────────────────────────────────────

static AstNode* parse_contract(Parser* p, AstNode** pre, AstNode** post) {
    *pre = NULL;
    *post = NULL;

    // Check for [[post] form (double-open = no pre)
    if (match(p, TOK_DBL_OPEN)) {
        // Parse postcondition expression
        *post = parse_expr(p);
        expect(p, TOK_RBRACK);
        return *post;
    }

    // Single [
    expect(p, TOK_LBRACK);
    // Parse precondition expression
    *pre = parse_expr(p);

    // Check for [pre]] form (double-close = no post)
    if (match(p, TOK_DBL_CLOSE)) {
        return *pre;
    }

    // Single ]
    expect(p, TOK_RBRACK);

    // Check for [pre][post] form
    if (match(p, TOK_LBRACK)) {
        *post = parse_expr(p);
        expect(p, TOK_RBRACK);
    }

    return *pre ? *pre : *post;
}

// ── Struct declaration ───────────────────────────────────────────────────

static AstNode* parse_struct_decl(Parser* p) {
    Token struct_tok = consume(p);
    AstNode* node = ast_alloc_node(NODE_STRUCT_DECL, struct_tok);

    if (check(p, TOK_IDENTIFIER)) {
        Token name_tok = consume(p);
        AstNode* name_node = ast_alloc_node(NODE_VARIABLE, name_tok);
        ast_add_child(node, name_node);
    }

    if (match(p, TOK_SEMI)) return node;

    expect(p, TOK_LBRACE);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        AstNode* field_type = parse_type(p);
        if (!field_type) break;

        if (!check(p, TOK_IDENTIFIER)) break;
        Token field_name = consume(p);

        AstNode* field = ast_alloc_node(NODE_STRUCT_FIELD, field_name);
        ast_add_child(field, field_type);

        if (match(p, TOK_LBRACK)) {
            AstNode* size = parse_expr(p);
            AstNode* sub = ast_alloc_node(NODE_ARRAY_SUB, size ? size->token : peek(p));
            if (size) ast_add_child(sub, size);
            ast_add_child(field, sub);
            expect(p, TOK_RBRACK);
        }

        expect(p, TOK_SEMI);
        ast_add_child(node, field);
    }

    expect(p, TOK_RBRACE);
    match(p, TOK_SEMI);
    return node;
}

// ── Union declaration ────────────────────────────────────────────────────

static AstNode* parse_union_decl(Parser* p) {
    Token union_tok = consume(p);
    AstNode* node = ast_alloc_node(NODE_UNION_DECL, union_tok);

    if (check(p, TOK_IDENTIFIER)) {
        Token name_tok = consume(p);
        AstNode* name_node = ast_alloc_node(NODE_VARIABLE, name_tok);
        ast_add_child(node, name_node);
    }

    if (match(p, TOK_SEMI)) return node;

    expect(p, TOK_LBRACE);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        AstNode* field_type = parse_type(p);
        if (!field_type) break;

        if (!check(p, TOK_IDENTIFIER)) break;
        Token field_name = consume(p);

        AstNode* field = ast_alloc_node(NODE_STRUCT_FIELD, field_name);
        ast_add_child(field, field_type);

        if (match(p, TOK_LBRACK)) {
            AstNode* size = parse_expr(p);
            AstNode* sub = ast_alloc_node(NODE_ARRAY_SUB, size ? size->token : peek(p));
            if (size) ast_add_child(sub, size);
            ast_add_child(field, sub);
            expect(p, TOK_RBRACK);
        }

        expect(p, TOK_SEMI);
        ast_add_child(node, field);
    }

    expect(p, TOK_RBRACE);
    match(p, TOK_SEMI);
    return node;
}

// ── Enum declaration ─────────────────────────────────────────────────────

static AstNode* parse_enum_decl(Parser* p) {
    Token enum_tok = consume(p);
    AstNode* node = ast_alloc_node(NODE_ENUM_DECL, enum_tok);

    if (check(p, TOK_IDENTIFIER)) {
        Token name_tok = consume(p);
        AstNode* name_node = ast_alloc_node(NODE_VARIABLE, name_tok);
        ast_add_child(node, name_node);
    }

    if (match(p, TOK_SEMI)) return node;

    expect(p, TOK_LBRACE);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token name = expect(p, TOK_IDENTIFIER);
        if (name.kind == TOK_ERROR) break;

        AstNode* value = NULL;
        if (match(p, TOK_ASSIGN)) {
            value = parse_expr(p);
        }

        AstNode* enumerator = ast_alloc_node(NODE_STRUCT_FIELD, name);
        if (value) ast_add_child(enumerator, value);
        ast_add_child(node, enumerator);

        if (!match(p, TOK_COMMA)) break;
    }

    expect(p, TOK_RBRACE);
    match(p, TOK_SEMI);
    return node;
}

// ── Typedef ───────────────────────────────────────────────────────────────

static AstNode* parse_typedef(Parser* p) {
    Token typedef_tok = consume(p);
    AstNode* node = ast_alloc_node(NODE_TYPEDEF, typedef_tok);

    AstNode* type = NULL;
    if (check(p, TOK_STRUCT)) {
        type = parse_struct_decl(p);
    } else if (check(p, TOK_UNION)) {
        type = parse_union_decl(p);
    } else if (check(p, TOK_ENUM)) {
        type = parse_enum_decl(p);
    } else {
        type = parse_type(p);
    }

    if (!type) {
        errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc, "expected type in typedef");
        return NULL;
    }
    ast_add_child(node, type);

    Token name = expect(p, TOK_IDENTIFIER);
    if (name.kind != TOK_ERROR) {
        AstNode* name_node = ast_alloc_node(NODE_VARIABLE, name);
        ast_add_child(node, name_node);
    }

    expect(p, TOK_SEMI);
    return node;
}

// ── Function signature ──────────────────────────────────────────────────

static AstNode* parse_parameter_list(Parser* p) {
    AstNode* list = ast_alloc_node(NODE_PARAM_LIST, peek(p));
    if (check(p, TOK_RPAREN)) return list;

    do {
        AstNode* param = parse_parameter(p);
        if (param) ast_add_child(list, param);
    } while (match(p, TOK_COMMA));

    return list;
}

static AstNode* parse_parameter(Parser* p) {
    // Check for borrow/own modifier
    int is_borrow = 0;
    int is_own = 0;
    if (match(p, TOK_BORROW)) {
        is_borrow = 1;
    } else if (match(p, TOK_OWN)) {
        is_own = 1;
    }

    // Parse type
    AstNode* type = parse_type(p);
    if (!type) return NULL;

    // Parse parameter name (optional for abstract declarations)
    AstNode* param = NULL;
    if (check(p, TOK_IDENTIFIER)) {
        Token name = consume(p);
        param = ast_alloc_node(NODE_DECL, name);
        ast_add_child(param, type);
        if (is_borrow) {
            AstNode* b = ast_alloc_node(NODE_BORROW_PARAM, name);
            ast_add_child(param, b);
        } else if (is_own) {
            AstNode* o = ast_alloc_node(NODE_OWN_PARAM, name);
            ast_add_child(param, o);
        }
    }

    return param ? param : type;
}

// ── Derivation block ────────────────────────────────────────────────────

static AstNode* parse_derivation_block(Parser* p) {
    AstNode* deriv = ast_alloc_node(NODE_DERIVATION, peek(p));
    expect(p, TOK_LBRACE);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        // Parse input-output example: input, input, ... -> output;
        AstNode* example = ast_alloc_node(NODE_DERIV_EXAMPLE, peek(p));

        // Parse inputs (comma-separated expressions)
        do {
            AstNode* input = parse_expr(p);
            if (input) ast_add_child(example, input);
        } while (match(p, TOK_COMMA));

        // Arrow
        if (!match(p, TOK_ARROW)) {
            errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
                        "expected '->' in derivation example");
            break;
        }

        // Optional tolerance: [tolerance] output
        AstNode* tolerance_node = NULL;
        if (match(p, TOK_LBRACK)) {
            AstNode* tol_expr = parse_expr(p);
            expect(p, TOK_RBRACK);
            if (tol_expr) {
                tolerance_node = ast_alloc_node(NODE_DERIV_TOLERANCE, tol_expr->token);
                ast_add_child(tolerance_node, tol_expr);
            }
        }

        // Output expression
        AstNode* output = parse_expr(p);
        if (output) ast_add_child(example, output);
        if (tolerance_node) ast_add_child(example, tolerance_node);

        // Semicolon separator
        if (!match(p, TOK_SEMI)) {
            // Allow missing semicolon before closing brace
            if (!check(p, TOK_RBRACE)) {
                errlist_add(p->errors, ERROR_LEVEL_WARNING, peek(p).loc,
                            "expected ';' after derivation example");
            }
        }

        ast_add_child(deriv, example);
    }

    expect(p, TOK_RBRACE);
    // Optional semicolon after the closing brace in `:= { ... };`
    match(p, TOK_SEMI);
    return deriv;
}

// ── Block ───────────────────────────────────────────────────────────────

static AstNode* parse_block(Parser* p) {
    AstNode* block = ast_alloc_node(NODE_BLOCK, peek(p));
    // Opening { already consumed by caller

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        AstNode* stmt = parse_statement(p);
        if (stmt) {
            ast_add_child(block, stmt);
        } else {
            synchronize(p);
        }
    }

    expect(p, TOK_RBRACE);
    return block;
}

// ── Statements ──────────────────────────────────────────────────────────

static AstNode* parse_statement(Parser* p) {
    // when condition -> statement;
    if (check(p, TOK_WHEN)) {
        Token when_tok = consume(p);
        AstNode* cond = parse_expr(p);
        AstNode* stmt = NULL;

        if (match(p, TOK_ARROW)) {
            // Single-line when
            stmt = parse_statement(p);
        } else if (match(p, TOK_LBRACE)) {
            // Multi-line when block
            stmt = parse_block(p);
        }

        return ast_make_when(cond, stmt, when_tok.loc);
    }

    // if/else are deliberately excluded from C² — use `when`
    if (match(p, TOK_IF)) {
        errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
            "`if` is not a C² construct.\n"
            "  Use `when` instead:\n"
            "    when condition -> expression;    // single expression (arrow)\n"
            "    when condition { statements }    // block body (braces)\n"
            "  Use sequential `when` guards in place of if/else chains:\n"
            "    when x > 0 -> positive();\n"
            "    when x < 0 -> negative();");
        // Recover: skip past the if (...) body
        if (match(p, TOK_LPAREN)) {
            int depth = 1;
            while (depth > 0 && !check(p, TOK_EOF)) {
                if (match(p, TOK_LPAREN)) depth++;
                else if (match(p, TOK_RPAREN)) depth--;
                else consume(p);
            }
        }
        while (!check(p, TOK_SEMI) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF))
            consume(p);
        if (check(p, TOK_SEMI)) consume(p);
        return NULL;
    }
    if (match(p, TOK_ELSE)) {
        errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
            "`else` without a matching `when` — use sequential `when` guards:\n"
            "    when x -> A;\n"
            "    when !x -> B;");
        // Recover: skip this else's statement
        while (!check(p, TOK_SEMI) && !check(p, TOK_RBRACE) && !check(p, TOK_RBRACK) && !check(p, TOK_EOF))
            consume(p);
        if (check(p, TOK_SEMI)) consume(p);
        return NULL;
    }

    // while (expr) stmt
    if (match(p, TOK_WHILE)) {
        expect(p, TOK_LPAREN);
        AstNode* cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        AstNode* body = parse_statement(p);
        AstNode* node = ast_alloc_node(NODE_WHILE, cond ? cond->token : peek(p));
        if (cond) ast_add_child(node, cond);
        if (body) ast_add_child(node, body);
        return node;
    }

    // for (init; cond; inc) stmt
    if (match(p, TOK_FOR)) {
        expect(p, TOK_LPAREN);
        AstNode* node = ast_alloc_node(NODE_FOR, peek(p));
        // init (expression or declaration)
        if (!check(p, TOK_SEMI)) {
            AstNode* init = parse_expr(p);
            if (init) ast_add_child(node, init);
        }
        expect(p, TOK_SEMI);
        // condition
        if (!check(p, TOK_SEMI)) {
            AstNode* cond = parse_expr(p);
            if (cond) ast_add_child(node, cond);
        }
        expect(p, TOK_SEMI);
        // increment
        if (!check(p, TOK_RPAREN)) {
            AstNode* inc = parse_expr(p);
            if (inc) ast_add_child(node, inc);
        }
        expect(p, TOK_RPAREN);
        AstNode* body = parse_statement(p);
        if (body) ast_add_child(node, body);
        return node;
    }

    // return expr;
    if (match(p, TOK_RETURN)) {
        AstNode* node = ast_alloc_node(NODE_RETURN, peek(p));
        if (!check(p, TOK_SEMI)) {
            AstNode* expr = parse_expr(p);
            if (expr) ast_add_child(node, expr);
        }
        expect(p, TOK_SEMI);
        return node;
    }

    // break;
    if (match(p, TOK_BREAK)) {
        AstNode* node = ast_alloc_node(NODE_BREAK, peek(p));
        expect(p, TOK_SEMI);
        return node;
    }

    // continue;
    if (match(p, TOK_CONTINUE)) {
        AstNode* node = ast_alloc_node(NODE_CONTINUE, peek(p));
        expect(p, TOK_SEMI);
        return node;
    }

    // case expr:
    if (match(p, TOK_CASE)) {
        Token case_tok = peek(p);
        AstNode* expr = parse_expr(p);
        expect(p, TOK_COLON);
        AstNode* node = ast_alloc_node(NODE_CASE, case_tok);
        if (expr) ast_add_child(node, expr);
        return node;
    }

    // default:
    if (match(p, TOK_DEFAULT)) {
        expect(p, TOK_COLON);
        return ast_alloc_node(NODE_DEFAULT, peek(p));
    }

    // switch (expr) { ... }
    if (match(p, TOK_SWITCH)) {
        expect(p, TOK_LPAREN);
        AstNode* cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        if (check(p, TOK_LBRACE)) {
            Token sw_tok = cond ? cond->token : peek(p);
            consume(p); // {
            AstNode* node = ast_alloc_node(NODE_SWITCH, sw_tok);
            if (cond) ast_add_child(node, cond);
            while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                AstNode* stmt = parse_statement(p);
                if (stmt) ast_add_child(node, stmt);
            }
            expect(p, TOK_RBRACE);
            return node;
        }
        errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
            "switch body must be a block { ... }");
        return NULL;
    }

    // goto is deliberately excluded from C²
    if (match(p, TOK_GOTO)) {
        errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
            "`goto` is not a C² construct.\n"
            "  C² enforces flat control flow. Use instead:\n"
            "    when guards for conditional branching\n"
            "    break/continue for loop control\n"
            "  For complex state machines, import a regular C file.");
        // Recover: consume 'goto label;'
        while (!check(p, TOK_SEMI) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF))
            consume(p);
        if (check(p, TOK_SEMI)) consume(p);
        return NULL;
    }

    // ; (empty statement)
    if (match(p, TOK_SEMI)) return NULL;

    // { ... } (block)
    if (match(p, TOK_LBRACE)) return parse_block(p);

    // Variable declaration: look for `type name ...;` pattern
    // Only attempt if current token starts a type AND next token is an identifier or *
    int could_be_decl = 0;
    if (is_type_token(p->lexer.current_tok.kind) || check(p, TOK_IDENTIFIER)) {
        Token nxt = peek(p);
        if (nxt.kind == TOK_IDENTIFIER || nxt.kind == TOK_STAR) {
            could_be_decl = 1;
        }
    }
    if (could_be_decl) {
        AstNode* vtype = parse_type(p);
        if (vtype && check(p, TOK_IDENTIFIER)) {
            Token name = consume(p);
            AstNode* decl = ast_alloc_node(NODE_DECL, name);
            ast_add_child(decl, vtype);

            if (match(p, TOK_LBRACK)) {
                AstNode* size = parse_expr(p);
                AstNode* sub = ast_alloc_node(NODE_ARRAY_SUB, size ? size->token : peek(p));
                if (size) ast_add_child(sub, size);
                ast_add_child(decl, sub);
                expect(p, TOK_RBRACK);
            }

            if (match(p, TOK_ASSIGN)) {
                AstNode* init = parse_expr(p);
                if (init) ast_add_child(decl, init);
            }

            expect(p, TOK_SEMI);
            AstNode* stmt = ast_alloc_node(NODE_EXPR_STMT, decl->token);
            ast_add_child(stmt, decl);
            return stmt;
        }
        // parse_type consumed some tokens but it wasn't a declaration —
        // fall through to expression parsing with whatever's left
    }

    // Expression statement
    AstNode* expr = parse_expr(p);
    if (expr) {
        expect(p, TOK_SEMI);
        AstNode* stmt = ast_alloc_node(NODE_EXPR_STMT, expr->token);
        ast_add_child(stmt, expr);
        return stmt;
    }
    return expr;
}

// ── Expression parsing (standard C precedence climbing) ─────────────────

// Expressions are parsed with standard C precedence:
//   assignment → conditional → logical_or → logical_and → bit_or
//   → bit_xor → bit_and → equality → relational → shift → term → factor → unary → postfix → primary

static AstNode* parse_expr(Parser* p) {
    return parse_assignment(p);
}

static AstNode* parse_assignment(Parser* p) {
    AstNode* left = parse_conditional(p);
    if (!left) return NULL;

    TokenKind op_kind = p->lexer.current_tok.kind;
    if (op_kind == TOK_ASSIGN || op_kind == TOK_ADD_ASSIGN || op_kind == TOK_SUB_ASSIGN ||
        op_kind == TOK_MUL_ASSIGN || op_kind == TOK_DIV_ASSIGN || op_kind == TOK_MOD_ASSIGN ||
        op_kind == TOK_AND_ASSIGN || op_kind == TOK_OR_ASSIGN || op_kind == TOK_XOR_ASSIGN ||
        op_kind == TOK_SHL_ASSIGN || op_kind == TOK_SHR_ASSIGN) {
        Token op = consume(p);
        AstNode* right = parse_assignment(p);
        AstNode* node = ast_alloc_node(NODE_ASSIGN, op);
        ast_add_child(node, left);
        if (right) ast_add_child(node, right);
        return node;
    }

    return left;
}

static AstNode* parse_conditional(Parser* p) {
    AstNode* cond = parse_logical_or(p);
    if (!cond) return NULL;

    if (match(p, TOK_QUESTION)) {
        AstNode* then_branch = parse_expr(p);
        expect(p, TOK_COLON);
        AstNode* else_branch = parse_conditional(p);
        AstNode* node = ast_alloc_node(NODE_TERNARY, cond->token);
        ast_add_child(node, cond);
        if (then_branch) ast_add_child(node, then_branch);
        if (else_branch) ast_add_child(node, else_branch);
        return node;
    }

    return cond;
}

static AstNode* parse_logical_or(Parser* p) {
    AstNode* left = parse_logical_and(p);
    if (!left) return NULL;

    while (match(p, TOK_OR)) {
        AstNode* right = parse_logical_and(p);
        if (!right) break;
        AstNode* node = ast_alloc_node(NODE_BINARY_OP, left->token);
        node->token.kind = TOK_OR;
        ast_add_child(node, left);
        ast_add_child(node, right);
        left = node;
    }

    return left;
}

static AstNode* parse_logical_and(Parser* p) {
    AstNode* left = parse_bit_or(p);
    if (!left) return NULL;

    while (match(p, TOK_AND)) {
        AstNode* right = parse_bit_or(p);
        if (!right) break;
        AstNode* node = ast_alloc_node(NODE_BINARY_OP, left->token);
        node->token.kind = TOK_AND;
        ast_add_child(node, left);
        ast_add_child(node, right);
        left = node;
    }

    return left;
}

static AstNode* parse_bit_or(Parser* p) {
    AstNode* left = parse_bit_xor(p);
    if (!left) return NULL;

    while (match(p, TOK_BIT_OR)) {
        AstNode* right = parse_bit_xor(p);
        if (!right) break;
        left = ast_make_binary_op(TOK_BIT_OR, left, right, left->token.loc);
    }

    return left;
}

static AstNode* parse_bit_xor(Parser* p) {
    AstNode* left = parse_bit_and(p);
    if (!left) return NULL;

    while (match(p, TOK_BIT_XOR)) {
        AstNode* right = parse_bit_and(p);
        if (!right) break;
        left = ast_make_binary_op(TOK_BIT_XOR, left, right, left->token.loc);
    }

    return left;
}

static AstNode* parse_bit_and(Parser* p) {
    AstNode* left = parse_equality(p);
    if (!left) return NULL;

    while (match(p, TOK_BIT_AND)) {
        AstNode* right = parse_equality(p);
        if (!right) break;
        left = ast_make_binary_op(TOK_BIT_AND, left, right, left->token.loc);
    }

    return left;
}

static AstNode* parse_equality(Parser* p) {
    AstNode* left = parse_relational(p);
    if (!left) return NULL;

    while (check(p, TOK_EQ) || check(p, TOK_NE)) {
        Token op = consume(p);
        AstNode* right = parse_relational(p);
        if (!right) break;
        left = ast_make_binary_op(op.kind, left, right, op.loc);
    }

    return left;
}

static AstNode* parse_relational(Parser* p) {
    AstNode* left = parse_shift(p);
    if (!left) return NULL;

    while (check(p, TOK_LT) || check(p, TOK_GT) || check(p, TOK_LE) || check(p, TOK_GE)) {
        Token op = consume(p);
        AstNode* right = parse_shift(p);
        if (!right) break;
        left = ast_make_binary_op(op.kind, left, right, op.loc);
    }

    return left;
}

static AstNode* parse_shift(Parser* p) {
    AstNode* left = parse_term(p);
    if (!left) return NULL;

    while (check(p, TOK_SHL) || check(p, TOK_SHR)) {
        Token op = consume(p);
        AstNode* right = parse_term(p);
        if (!right) break;
        left = ast_make_binary_op(op.kind, left, right, op.loc);
    }

    return left;
}

static AstNode* parse_term(Parser* p) {
    AstNode* left = parse_factor(p);
    if (!left) return NULL;

    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token op = consume(p);
        AstNode* right = parse_factor(p);
        if (!right) break;
        left = ast_make_binary_op(op.kind, left, right, op.loc);
    }

    return left;
}

static AstNode* parse_factor(Parser* p) {
    AstNode* left = parse_unary(p);
    if (!left) return NULL;

    while (check(p, TOK_STAR) || check(p, TOK_DIV) || check(p, TOK_MOD)) {
        Token op = consume(p);
        AstNode* right = parse_unary(p);
        if (!right) break;
        left = ast_make_binary_op(op.kind, left, right, op.loc);
    }

    return left;
}

static AstNode* parse_unary(Parser* p) {
    TokenKind unary_ops[] = {
        TOK_MINUS, TOK_PLUS, TOK_NOT, TOK_BIT_NOT,
        TOK_INC, TOK_DEC, TOK_STAR, TOK_BIT_AND,
        TOK_SIZEOF,
    };
    size_t num_ops = sizeof(unary_ops) / sizeof(unary_ops[0]);

    for (size_t i = 0; i < num_ops; i++) {
        if (check(p, unary_ops[i])) {
            Token op = consume(p);

            // sizeof has special syntax: sizeof(type) or sizeof expr
            if (op.kind == TOK_SIZEOF) {
                AstNode* node = ast_alloc_node(NODE_SIZEOF, op);
                if (match(p, TOK_LPAREN)) {
                    // Could be type or expression
                    AstNode* inner = parse_expr(p);
                    if (inner) ast_add_child(node, inner);
                    expect(p, TOK_RPAREN);
                } else {
                    AstNode* inner = parse_unary(p);
                    if (inner) ast_add_child(node, inner);
                }
                return node;
            }

            AstNode* operand = parse_unary(p);
            if (!operand) return NULL;
            AstNode* node = ast_alloc_node(NODE_UNARY_OP, op);
            ast_add_child(node, operand);
            return node;
        }
    }

    return parse_postfix(p);
}

static AstNode* parse_postfix(Parser* p) {
    AstNode* left = parse_primary(p);
    if (!left) return NULL;

    while (1) {
        if (match(p, TOK_LBRACK)) {
            // Array index
            AstNode* index = parse_expr(p);
            expect(p, TOK_RBRACK);
            AstNode* node = ast_alloc_node(NODE_INDEX, left->token);
            ast_add_child(node, left);
            if (index) ast_add_child(node, index);
            left = node;
        } else if (match(p, TOK_LPAREN)) {
            // Function call
            AstNode* node = ast_alloc_node(NODE_CALL, left->token);
            ast_add_child(node, left);
            if (!check(p, TOK_RPAREN)) {
                do {
                    AstNode* arg = parse_expr(p);
                    if (arg) ast_add_child(node, arg);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN);
            left = node;
        } else if (match(p, TOK_DOT)) {
            // Member access
            Token name = expect(p, TOK_IDENTIFIER);
            if (name.kind != TOK_ERROR) {
                AstNode* node = ast_alloc_node(NODE_MEMBER, left->token);
                ast_add_child(node, left);
                AstNode* member = ast_make_variable(name);
                ast_add_child(node, member);
                left = node;
            }
        } else if (check(p, TOK_ARROW)) {
            // Pointer member access — only consume if followed by identifier
            // (avoid eating -> in when guards)
            Token next = peek(p);
            if (next.kind != TOK_IDENTIFIER) break;
            consume(p);
            Token name = expect(p, TOK_IDENTIFIER);
            if (name.kind != TOK_ERROR) {
                AstNode* node = ast_alloc_node(NODE_DEREF, left->token);
                ast_add_child(node, left);
                AstNode* member = ast_make_variable(name);
                ast_add_child(node, member);
                left = node;
            }
        } else if (match(p, TOK_INC) || match(p, TOK_DEC)) {
            AstNode* node = ast_alloc_node(NODE_UNARY_OP, left->token);
            ast_add_child(node, left);
            left = node;
        } else {
            break;
        }
    }

    return left;
}

static AstNode* parse_primary(Parser* p) {
    if (match(p, TOK_LPAREN)) {
        AstNode* expr = parse_expr(p);
        expect(p, TOK_RPAREN);
        return expr;
    }

    if (check(p, TOK_INT_LITERAL)) {
        Token t = consume(p);
        return ast_make_literal_int(t.value.i64, t.loc);
    }

    if (check(p, TOK_FLOAT_LITERAL)) {
        Token t = consume(p);
        AstNode* node = ast_alloc_node(NODE_LITERAL_FLOAT, t);
        return node;
    }

    if (check(p, TOK_STRING_LITERAL)) {
        Token t = consume(p);
        return ast_alloc_node(NODE_LITERAL_STR, t);
    }

    if (check(p, TOK_CHAR_LITERAL)) {
        Token t = consume(p);
        return ast_alloc_node(NODE_LITERAL_CHAR, t);
    }

    if (check(p, TOK_IDENTIFIER)) {
        Token t = consume(p);
        // Check if followed by '(' — handled in postfix
        return ast_make_variable(t);
    }

    // Handle unary & and * in primary (they're actually in unary, but this
    // catches cases where they appear as primary expressions)
    if (check(p, TOK_BIT_AND) || check(p, TOK_STAR)) {
        return parse_unary(p);
    }

    errlist_add(p->errors, ERROR_LEVEL_ERROR, peek(p).loc,
                "expected expression");
    return NULL;
}

// ── Simplified type parsing ─────────────────────────────────────────────

static int is_type_token(TokenKind kind) {
    switch (kind) {
        case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT:
        case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE:
        case TOK_SIGNED: case TOK_UNSIGNED:
        case TOK_BOOL: case TOK_COMPLEX: case TOK_IMAGINARY:
        case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
        case TOK_CONST: case TOK_VOLATILE: case TOK_RESTRICT:
        case TOK_EXTERN: case TOK_STATIC: case TOK_INLINE:
        case TOK_TYPEDEF: case TOK_NORETURN: case TOK_ATOMIC:
        case TOK_AUTO: case TOK_REGISTER:
            return 1;
        default:
            return 0;
    }
}

static AstNode* parse_type(Parser* p) {
    if (!is_type_token(p->lexer.current_tok.kind) && !check(p, TOK_IDENTIFIER)) {
        return NULL;
    }

    int consumed_type_keyword = 0;
    Token type_tok;
    int has_type_tok = 0;
    int is_struct_union_enum = 0;
    while (is_type_token(p->lexer.current_tok.kind)) {
        type_tok = consume(p);
        consumed_type_keyword = 1;
        has_type_tok = 1;

        if (type_tok.kind == TOK_STRUCT || type_tok.kind == TOK_UNION || type_tok.kind == TOK_ENUM) {
            is_struct_union_enum = 1;
        }
    }

    if (is_struct_union_enum && check(p, TOK_IDENTIFIER)) {
        // For `struct Foo`, keep `struct` as the keyword token and add `Foo` as a child
        // (do NOT override type_tok)
        Token tag_tok = consume(p);
        AstNode* node = ast_alloc_node(NODE_VARIABLE, type_tok);
        AstNode* tag_node = ast_alloc_node(NODE_VARIABLE, tag_tok);
        ast_add_child(node, tag_node);
        has_type_tok = 1;
        consumed_type_keyword = 1;

        while (match(p, TOK_STAR)) {
            has_type_tok = 2; // marker: pointer to struct
        }

        return node;
    }

    if (!consumed_type_keyword && check(p, TOK_IDENTIFIER)) {
        type_tok = consume(p);
        has_type_tok = 1;
    }

    while (match(p, TOK_STAR)) {
        has_type_tok = 2; // marker: it's a pointer
    }

    if (!has_type_tok) return NULL;
    return ast_alloc_node(NODE_VARIABLE, type_tok);
}

// ── Parser utilities ────────────────────────────────────────────────────

static Token consume(Parser* p) {
    Token t = lexer_advance(&p->lexer);
    return t;
}

static Token expect(Parser* p, TokenKind kind) {
    if (check(p, kind)) {
        return consume(p);
    }
    Token t = peek(p);
    errlist_add(p->errors, ERROR_LEVEL_ERROR, t.loc,
                "expected '%s' but got '%s'",
                token_kind_name(kind), token_kind_name(t.kind));
    t.kind = TOK_ERROR;
    return t;
}

static int match(Parser* p, TokenKind kind) {
    if (check(p, kind)) {
        consume(p);
        return 1;
    }
    return 0;
}

static int check(Parser* p, TokenKind kind) {
    return lexer_check(&p->lexer, kind);
}

static Token peek(Parser* p) {
    return lexer_peek(&p->lexer);
}

static void synchronize(Parser* p) {
    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_SEMI)) {
            consume(p);
            return;
        }
        if (check(p, TOK_RBRACE)) {
            consume(p);
            return;
        }
        consume(p);
    }
}

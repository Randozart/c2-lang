// 2026-07-13 — AST node definitions for C² (Contract Enforced C).
//   Defines the node kinds and data structures used by the parser,
//   verifier, borrow checker, drop injector, and code generator.

#ifndef C2_AST_H
#define C2_AST_H

#include <stddef.h>
#include <stdint.h>
#include "lexer.h"

// ── Forward declarations ────────────────────────────────────────────────

typedef struct AstNode AstNode;
typedef struct Symbol Symbol;
typedef struct Type Type;

// ── Node kinds ──────────────────────────────────────────────────────────

typedef enum {
    // C²-specific nodes
    NODE_CONTRACT_PRE,
    NODE_CONTRACT_POST,
    NODE_DERIVATION,
    NODE_DERIV_EXAMPLE,
    NODE_DERIV_TOLERANCE,
    NODE_WHEN,
    NODE_BORROW_PARAM,
    NODE_OWN_PARAM,
    NODE_NO_DERIVE,

    // Standard C nodes
    NODE_FUNCTION,
    NODE_BLOCK,
    NODE_IF,
    NODE_ELSE,
    NODE_WHILE,
    NODE_DO_WHILE,
    NODE_FOR,
    NODE_RETURN,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_SWITCH,
    NODE_CASE,
    NODE_DEFAULT,
    NODE_CALL,
    NODE_INDEX,
    NODE_MEMBER,
    NODE_DEREF,
    NODE_ADDR_OF,
    NODE_UNARY_OP,
    NODE_BINARY_OP,
    NODE_ASSIGN,
    NODE_TERNARY,
    NODE_CAST,
    NODE_SIZEOF,
    NODE_LITERAL_INT,
    NODE_LITERAL_FLOAT,
    NODE_LITERAL_STR,
    NODE_LITERAL_CHAR,
    NODE_VARIABLE,
    NODE_DECL,
    NODE_PARAM_LIST,
    NODE_STRUCT_DECL,
    NODE_UNION_DECL,
    NODE_ENUM_DECL,
    NODE_STRUCT_FIELD,
    NODE_ARRAY_SUB,          // Array subscript marker in declarations (child = size)
    NODE_TYPEDEF,
    NODE_EXTERN,
    NODE_STATIC_ASSERT,
    NODE_TRANSLATION_UNIT,
    NODE_EXPR_STMT,          // Expression used as statement (wraps an expr + ;)
    NODE_DROP_CALL,          // Injected by drop pass (not in source)
    NODE_PP_INCLUDE,         // Preprocessor #include
    NODE_PP_DEFINE,          // Preprocessor #define
    NODE_PP_DIRECTIVE,       // Any other preprocessor directive
} NodeKind;

// ── Value range (VRP result) ────────────────────────────────────────────

typedef struct {
    int64_t lo;
    int64_t hi;
    int     has_range;  // 0 = no inferred range
} ValueRange;

// ── AST node ────────────────────────────────────────────────────────────

struct AstNode {
    NodeKind    kind;
    AstNode**   children;
    size_t      child_count;
    size_t      child_capacity;
    Token       token;
    Type*       type;       // Type-checked result (populated by type checker)
    ValueRange  range;      // VRP result (populated by VRP pass)
    Symbol*     symbol;     // Symbol table reference (for variable/function nodes)
    int         flags;      // Misc flags (e.g., is_lvalue, is_const)
};

// ── Node flags ─────────────────────────────────────────────────────────

#define NODE_FLAG_STATIC 1
#define NODE_FLAG_CONST  2

// ── AST construction functions ──────────────────────────────────────────

AstNode* ast_alloc_node(NodeKind kind, Token token);
void     ast_free_node(AstNode* node);
void     ast_add_child(AstNode* parent, AstNode* child);
void     ast_free_tree(AstNode* root);

// ── Convenience constructors ────────────────────────────────────────────

AstNode* ast_make_literal_int(int64_t value, SourceLoc loc);
AstNode* ast_make_variable(Token name_tok);
AstNode* ast_make_binary_op(int op_kind, AstNode* left, AstNode* right, SourceLoc loc);
AstNode* ast_make_function(Token name_tok, AstNode* ret_type,
                           AstNode* params, AstNode* body,
                           AstNode* pre, AstNode* post, AstNode* deriv);
AstNode* ast_make_block(SourceLoc loc);
AstNode* ast_make_when(AstNode* cond, AstNode* stmt, SourceLoc loc);

// ── AST traversal ───────────────────────────────────────────────────────

typedef int (*AstVisitor)(AstNode* node, void* user_data);

int ast_walk_preorder(AstNode* root, AstVisitor visitor, void* user_data);
int ast_walk_postorder(AstNode* root, AstVisitor visitor, void* user_data);

#endif // C2_AST_H

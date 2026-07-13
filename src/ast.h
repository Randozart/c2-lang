// 2026-07-13 — AST node definitions for C² (Contract Enforced C).
//   Defines the node kinds and data structures used by the parser,
//   verifier, borrow checker, drop injector, and code generator.

#ifndef C2_AST_H
#define C2_AST_H

#include <stddef.h>
#include <stdint.h>

// ── Forward declarations ────────────────────────────────────────────────

typedef struct AstNode AstNode;
typedef struct Token Token;
typedef struct Symbol Symbol;
typedef struct Type Type;

// ── Node kinds ──────────────────────────────────────────────────────────

typedef enum {
    // C²-specific nodes
    NODE_CONTRACT_PRE,
    NODE_CONTRACT_POST,
    NODE_DERIVATION,
    NODE_DERIV_EXAMPLE,
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
    NODE_TYPEDEF,
    NODE_EXTERN,
    NODE_STATIC_ASSERT,
    NODE_TRANSLATION_UNIT,
    NODE_DROP_CALL,          // Injected by drop pass (not in source)
} NodeKind;

// ── Value range (VRP result) ────────────────────────────────────────────

typedef struct {
    int64_t lo;
    int64_t hi;
    int     has_range;  // 0 = no inferred range
} ValueRange;

// ── Source location ─────────────────────────────────────────────────────

typedef struct {
    const char* filename;
    size_t      line;
    size_t      col;
    size_t      offset;  // Byte offset from start of file
} SourceLoc;

// ── Token (produced by lexer, stored in AST for error reporting) ────────

typedef struct {
    int         kind;
    SourceLoc   loc;
    const char* text;
    size_t      len;
    union {
        int64_t  i64;
        double   f64;
        char*    str;
    } value;
} Token;

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

// ── AST construction functions ──────────────────────────────────────────

AstNode* ast_alloc_node(NodeKind kind, Token token);
void     ast_free_node(AstNode* node);
void     ast_add_child(AstNode* parent, AstNode* child);
void     ast_free_tree(AstNode* root);

// ── Convenience constructors ────────────────────────────────────────────

AstNode* ast_make_literal_int(int64_t value, SourceLoc loc);
AstNode* ast_make_variable(const char* name, SourceLoc loc);
AstNode* ast_make_binary_op(int op_kind, AstNode* left, AstNode* right, SourceLoc loc);
AstNode* ast_make_function(const char* name, AstNode* params, AstNode* body,
                           AstNode* pre, AstNode* post, AstNode* deriv, SourceLoc loc);
AstNode* ast_make_block(SourceLoc loc);
AstNode* ast_make_when(AstNode* cond, AstNode* stmt, SourceLoc loc);

// ── AST traversal ───────────────────────────────────────────────────────

typedef int (*AstVisitor)(AstNode* node, void* user_data);

int ast_walk_preorder(AstNode* root, AstVisitor visitor, void* user_data);
int ast_walk_postorder(AstNode* root, AstVisitor visitor, void* user_data);

// ── Debug ───────────────────────────────────────────────────────────────

void ast_dump(AstNode* root, int indent);

#endif // C2_AST_H

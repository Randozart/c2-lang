// 2026-07-13 — Token definitions and lexer API for C².
//   Recognizes all C23 tokens plus C² extensions:
//     borrow, own, when, no_derive, :=, [[, ]]

#ifndef C2_LEXER_H
#define C2_LEXER_H

#include <stddef.h>
#include <stdint.h>

// ── Forward declarations ────────────────────────────────────────────────

typedef struct ErrorList ErrorList;

// ── Token kinds ─────────────────────────────────────────────────────────

typedef enum {
    // Special
    TOK_EOF = 0,
    TOK_ERROR,

    // C²-specific keywords
    TOK_BORROW,
    TOK_OWN,
    TOK_WHEN,
    TOK_NO_DERIVE,
    TOK_DERIVE,       // :=

    // C²-specific brackets
    TOK_DBL_OPEN,     // [[
    TOK_DBL_CLOSE,    // ]]

    // Standard C keywords
    TOK_IF, TOK_ELSE, TOK_SWITCH, TOK_CASE, TOK_DEFAULT,
    TOK_WHILE, TOK_DO, TOK_FOR,
    TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_GOTO,
    TOK_SIZEOF, TOK_TYPEDEF, TOK_EXTERN, TOK_STATIC,
    TOK_CONST, TOK_VOLATILE, TOK_RESTRICT,
    TOK_STRUCT, TOK_UNION, TOK_ENUM,
    TOK_VOID, TOK_CHAR, TOK_SHORT, TOK_INT, TOK_LONG,
    TOK_FLOAT, TOK_DOUBLE, TOK_SIGNED, TOK_UNSIGNED,
    TOK_BOOL, TOK_COMPLEX, TOK_IMAGINARY,
    TOK_INLINE, TOK_NORETURN, TOK_ATOMIC,
    TOK_REGISTER, TOK_AUTO,

    // Standard C punctuation
    TOK_LBRACE,   // {
    TOK_RBRACE,   // }
    TOK_LPAREN,   // (
    TOK_RPAREN,   // )
    TOK_LBRACK,   // [
    TOK_RBRACK,   // ]
    TOK_SEMI,     // ;
    TOK_COMMA,    // ,
    TOK_DOT,      // .
    TOK_ARROW,    // ->
    TOK_COLON,    // :

    // Standard C operators
    TOK_ASSIGN,        // =
    TOK_ADD_ASSIGN,    // +=
    TOK_SUB_ASSIGN,    // -=
    TOK_MUL_ASSIGN,    // *=
    TOK_DIV_ASSIGN,    // /=
    TOK_MOD_ASSIGN,    // %=
    TOK_AND_ASSIGN,    // &=
    TOK_OR_ASSIGN,     // |=
    TOK_XOR_ASSIGN,    // ^=
    TOK_SHL_ASSIGN,    // <<=
    TOK_SHR_ASSIGN,    // >>=
    TOK_PLUS,          // +
    TOK_MINUS,         // -
    TOK_STAR,          // *
    TOK_DIV,           // /
    TOK_MOD,           // %
    TOK_INC,           // ++
    TOK_DEC,           // --
    TOK_EQ,            // ==
    TOK_NE,            // !=
    TOK_LT,            // <
    TOK_GT,            // >
    TOK_LE,            // <=
    TOK_GE,            // >=
    TOK_AND,           // &&
    TOK_OR,            // ||
    TOK_NOT,           // !
    TOK_BIT_AND,       // &
    TOK_BIT_OR,        // |
    TOK_BIT_XOR,       // ^
    TOK_BIT_NOT,       // ~
    TOK_SHL,           // <<
    TOK_SHR,           // >>
    TOK_QUESTION,      // ?  (ternary)
    TOK_HASH,          // #

    // Literals
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_STRING_LITERAL,
    TOK_CHAR_LITERAL,

    // Preprocessor (passed through as raw tokens for now)
    TOK_PP_INCLUDE,
    TOK_PP_DEFINE,
    TOK_PP_IFDEF,
    TOK_PP_IFNDEF,
    TOK_PP_ENDIF,
    TOK_PP_ELSE,
    TOK_PP_IF,
    TOK_PP_LINE,
    TOK_PP_PRAGMA,
    TOK_PP_DIRECTIVE,  // any other #directive
} TokenKind;

// ── Source location ─────────────────────────────────────────────────────

typedef struct {
    const char* filename;
    size_t      line;
    size_t      col;
    size_t      offset;
} SourceLoc;

// ── Token ───────────────────────────────────────────────────────────────

typedef struct {
    TokenKind   kind;
    SourceLoc   loc;
    const char* text;       // Pointer into source (not owned)
    size_t      len;        // Length of token text
    union {
        int64_t   i64;
        double    f64;
    } value;                // Parsed literal value (for numeric tokens)
} Token;

// ── Lexer state ─────────────────────────────────────────────────────────

typedef struct {
    const char* start;      // Start of the current token in source
    const char* current;    // Current scan position
    const char* source;     // Full source text
    size_t      source_len;
    const char* filename;   // For source location reporting
    size_t      line;
    size_t      col;
    size_t      offset;
    ErrorList*  errors;
    Token       prev;       // Previously scanned token (for lookback)
    Token       current_tok;
    int         has_prev;   // Non-zero if prev is valid
} Lexer;

// ── Lexer API ───────────────────────────────────────────────────────────

/// Create a new lexer for the given source text.
Lexer lexer_create(const char* source, size_t source_len, const char* filename, ErrorList* errors);

/// Scan and return the next token.
Token lexer_scan(Lexer* lex);

/// Peek at the next token without consuming it.
Token lexer_peek(Lexer* lex);

/// Advance past the current token and return it.
/// Returns the token that was current before advancing.
Token lexer_advance(Lexer* lex);

/// Expect the next token to be of the given kind. If it is, consume and return it.
/// If not, report an error and return an error token.
Token lexer_expect(Lexer* lex, TokenKind kind);

/// Consume the next token if it matches the given kind. Returns 1 if consumed, 0 if not.
int lexer_match(Lexer* lex, TokenKind kind);

/// Check if the current token is of the given kind.
int lexer_check(Lexer* lex, TokenKind kind);

/// Return a human-readable name for a token kind.
const char* token_kind_name(TokenKind kind);

#endif // C2_LEXER_H

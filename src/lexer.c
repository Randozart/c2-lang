// 2026-07-13 — Hand-written lexer for C².
//   Tokenizes C23 source code with C² extensions:
//     borrow, own, when, no_derive, :=, [[, ]]
//   Error recovery: on unrecognized character, emits TOK_ERROR and continues.

#include "lexer.h"
#include "error.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ── Keyword map ─────────────────────────────────────────────────────────

typedef struct {
    const char* word;
    TokenKind   kind;
} KeywordEntry;

static const KeywordEntry keywords[] = {
    // C²-specific keywords
    {"borrow",    TOK_BORROW},
    {"own",       TOK_OWN},
    {"when",      TOK_WHEN},
    {"no_derive", TOK_NO_DERIVE},

    // Standard C keywords
    {"if",        TOK_IF},
    {"else",      TOK_ELSE},
    {"switch",    TOK_SWITCH},
    {"case",      TOK_CASE},
    {"default",   TOK_DEFAULT},
    {"while",     TOK_WHILE},
    {"do",        TOK_DO},
    {"for",       TOK_FOR},
    {"return",    TOK_RETURN},
    {"break",     TOK_BREAK},
    {"continue",  TOK_CONTINUE},
    {"goto",      TOK_GOTO},
    {"sizeof",    TOK_SIZEOF},
    {"typedef",   TOK_TYPEDEF},
    {"extern",    TOK_EXTERN},
    {"static",    TOK_STATIC},
    {"const",     TOK_CONST},
    {"volatile",  TOK_VOLATILE},
    {"restrict",  TOK_RESTRICT},
    {"struct",    TOK_STRUCT},
    {"union",     TOK_UNION},
    {"enum",      TOK_ENUM},
    {"void",      TOK_VOID},
    {"char",      TOK_CHAR},
    {"short",     TOK_SHORT},
    {"int",       TOK_INT},
    {"long",      TOK_LONG},
    {"float",     TOK_FLOAT},
    {"double",    TOK_DOUBLE},
    {"signed",    TOK_SIGNED},
    {"unsigned",  TOK_UNSIGNED},
    {"_Bool",     TOK_BOOL},
    {"_Complex",  TOK_COMPLEX},
    {"_Imaginary",TOK_IMAGINARY},
    {"inline",    TOK_INLINE},
    {"_Noreturn", TOK_NORETURN},
    {"_Atomic",   TOK_ATOMIC},
    {"register",  TOK_REGISTER},
    {"auto",      TOK_AUTO},
};

#define NUM_KEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

// ── Forward declarations ────────────────────────────────────────────────

static Token scan_token(Lexer* lex);
static Token make_token(Lexer* lex, TokenKind kind);
static Token make_error_token(Lexer* lex, const char* msg);
static void  skip_whitespace(Lexer* lex);
static int   skip_comment(Lexer* lex);
static Token scan_string(Lexer* lex, int delimiter);
static Token scan_number(Lexer* lex);
static Token scan_identifier(Lexer* lex);
static Token check_keyword(Lexer* lex, const char* start, size_t len);
static Token scan_preprocessor(Lexer* lex);

// ── Lexer creation ──────────────────────────────────────────────────────

Lexer lexer_create(const char* source, size_t source_len, const char* filename, ErrorList* errors) {
    Lexer lex;
    lex.source = source;
    lex.source_len = source_len;
    lex.start = source;
    lex.current = source;
    lex.filename = filename;
    lex.line = 1;
    lex.col = 1;
    lex.offset = 0;
    lex.errors = errors;
    lex.has_prev = 0;
    memset(&lex.prev, 0, sizeof(lex.prev));
    memset(&lex.current_tok, 0, sizeof(lex.current_tok));
    return lex;
}

// ── Core scanning ───────────────────────────────────────────────────────

Token lexer_scan(Lexer* lex) {
    skip_whitespace(lex);
    lex->start = lex->current;

    if (lex->offset >= lex->source_len) {
        Token eof = make_token(lex, TOK_EOF);
        lex->has_prev = 1;
        lex->prev = lex->current_tok;
        lex->current_tok = eof;
        return eof;
    }

    Token token = scan_token(lex);
    lex->has_prev = 1;
    lex->prev = lex->current_tok;
    lex->current_tok = token;
    return token;
}

Token lexer_peek(Lexer* lex) {
    // Save full state, scan, restore
    const char* saved_current = lex->current;
    size_t saved_offset = lex->offset;
    size_t saved_line = lex->line;
    size_t saved_col = lex->col;
    const char* saved_start = lex->start;
    Token saved_current_tok = lex->current_tok;
    Token saved_prev = lex->prev;
    int saved_has_prev = lex->has_prev;

    Token t = lexer_scan(lex);

    lex->current = saved_current;
    lex->offset = saved_offset;
    lex->line = saved_line;
    lex->col = saved_col;
    lex->start = saved_start;
    lex->current_tok = saved_current_tok;
    lex->prev = saved_prev;
    lex->has_prev = saved_has_prev;

    return t;
}

Token lexer_advance(Lexer* lex) {
    Token t = lex->current_tok;
    lexer_scan(lex);
    return t;
}

Token lexer_expect(Lexer* lex, TokenKind kind) {
    Token t = lex->current_tok;
    if (t.kind != kind) {
        errlist_add(lex->errors, ERROR_LEVEL_ERROR, t.loc,
                    "expected '%s' but got '%s'",
                    token_kind_name(kind), token_kind_name(t.kind));
        Token err = {0};
        err.kind = TOK_ERROR;
        return err;
    }
    lexer_scan(lex);
    return t;
}

int lexer_match(Lexer* lex, TokenKind kind) {
    if (lex->current_tok.kind == kind) {
        lexer_scan(lex);
        return 1;
    }
    return 0;
}

int lexer_check(Lexer* lex, TokenKind kind) {
    return lex->current_tok.kind == kind;
}

// ── Internal helpers ────────────────────────────────────────────────────

static Token make_token(Lexer* lex, TokenKind kind) {
    Token t;
    t.kind = kind;
    t.loc.filename = lex->filename;
    t.loc.line = lex->line;
    t.loc.col = lex->col;
    t.loc.offset = (size_t)(lex->start - lex->source);
    t.text = lex->start;
    t.len = (size_t)(lex->current - lex->start);
    t.value.i64 = 0;
    t.value.f64 = 0.0;
    return t;
}

static Token make_error_token(Lexer* lex, const char* msg) {
    Token t = make_token(lex, TOK_ERROR);
    errlist_add(lex->errors, ERROR_LEVEL_ERROR, t.loc, "%s", msg);
    return t;
}

static int is_at_end(Lexer* lex) {
    return lex->offset >= lex->source_len;
}

static char advance(Lexer* lex) {
    char c = lex->source[lex->offset];
    lex->offset++;
    lex->current++;
    lex->col++;
    return c;
}

static char peek(Lexer* lex) {
    if (is_at_end(lex)) return '\0';
    return lex->source[lex->offset];
}

static char peek_next(Lexer* lex) {
    if (lex->offset + 1 >= lex->source_len) return '\0';
    return lex->source[lex->offset + 1];
}

static int match_char(Lexer* lex, char expected) {
    if (is_at_end(lex)) return 0;
    if (lex->source[lex->offset] != expected) return 0;
    advance(lex);
    return 1;
}

static void skip_whitespace(Lexer* lex) {
    for (;;) {
        char c = peek(lex);
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
                advance(lex);
                break;
            case '\n':
                advance(lex);
                lex->line++;
                lex->col = 1;
                break;
            case '/':
                if (!skip_comment(lex)) return;
                break;
            default:
                return;
        }
    }
}

static int skip_comment(Lexer* lex) {
    if (peek_next(lex) == '/') {
        // Line comment
        advance(lex);
        advance(lex);
        while (!is_at_end(lex) && peek(lex) != '\n') {
            advance(lex);
        }
        return 1;
    }
    if (peek_next(lex) == '*') {
        // Block comment
        advance(lex);
        advance(lex);
        while (!is_at_end(lex)) {
            if (peek(lex) == '\n') {
                lex->line++;
                lex->col = 1;
            }
            if (peek(lex) == '*' && peek_next(lex) == '/') {
                advance(lex);
                advance(lex);
                return 1;
            }
            advance(lex);
        }
        errlist_add(lex->errors, ERROR_LEVEL_ERROR,
                    make_token(lex, TOK_ERROR).loc,
                    "unterminated block comment");
        return 1;
    }
    return 0;
}

// ── Token scanning ──────────────────────────────────────────────────────

static Token scan_token(Lexer* lex) {
    char c = advance(lex);

    switch (c) {
        // Single-character tokens
        case '(': return make_token(lex, TOK_LPAREN);
        case ')': return make_token(lex, TOK_RPAREN);
        case '{': return make_token(lex, TOK_LBRACE);
        case '}': return make_token(lex, TOK_RBRACE);
        case ';': return make_token(lex, TOK_SEMI);
        case ',': return make_token(lex, TOK_COMMA);
        case '?': return make_token(lex, TOK_QUESTION);
        case '~': return make_token(lex, TOK_BIT_NOT);

        case '.':
            if (peek(lex) == '.' && peek_next(lex) == '.') {
                // ... (variadic) — not a named token, just return as identifier-like
                advance(lex); advance(lex);
                return make_token(lex, TOK_DOT);  // Represent as ...
            }
            return make_token(lex, TOK_DOT);

        // Brackets — check for C² double-bracket forms
        case '[':
            if (peek(lex) == '[') {
                advance(lex);
                return make_token(lex, TOK_DBL_OPEN);
            }
            return make_token(lex, TOK_LBRACK);

        case ']':
            if (peek(lex) == ']') {
                advance(lex);
                return make_token(lex, TOK_DBL_CLOSE);
            }
            return make_token(lex, TOK_RBRACK);

        // Operators with potential multi-character forms
        case '=':
            if (match_char(lex, '=')) return make_token(lex, TOK_EQ);
            return make_token(lex, TOK_ASSIGN);

        case '!':
            if (match_char(lex, '=')) return make_token(lex, TOK_NE);
            return make_token(lex, TOK_NOT);

        case '<':
            if (match_char(lex, '<')) {
                if (match_char(lex, '=')) return make_token(lex, TOK_SHL_ASSIGN);
                return make_token(lex, TOK_SHL);
            }
            if (match_char(lex, '=')) return make_token(lex, TOK_LE);
            return make_token(lex, TOK_LT);

        case '>':
            if (match_char(lex, '>')) {
                if (match_char(lex, '=')) return make_token(lex, TOK_SHR_ASSIGN);
                return make_token(lex, TOK_SHR);
            }
            if (match_char(lex, '=')) return make_token(lex, TOK_GE);
            return make_token(lex, TOK_GT);

        case '+':
            if (match_char(lex, '+')) return make_token(lex, TOK_INC);
            if (match_char(lex, '=')) return make_token(lex, TOK_ADD_ASSIGN);
            return make_token(lex, TOK_PLUS);

        case '-':
            if (match_char(lex, '-')) return make_token(lex, TOK_DEC);
            if (match_char(lex, '=')) return make_token(lex, TOK_SUB_ASSIGN);
            if (match_char(lex, '>')) return make_token(lex, TOK_ARROW);
            return make_token(lex, TOK_MINUS);

        case '*':
            if (match_char(lex, '=')) return make_token(lex, TOK_MUL_ASSIGN);
            return make_token(lex, TOK_STAR);

        case '/':
            if (match_char(lex, '=')) return make_token(lex, TOK_DIV_ASSIGN);
            // Should not reach here — comments handled in skip_whitespace
            return make_token(lex, TOK_DIV);

        case '%':
            if (match_char(lex, '=')) return make_token(lex, TOK_MOD_ASSIGN);
            return make_token(lex, TOK_MOD);

        case '&':
            if (match_char(lex, '&')) return make_token(lex, TOK_AND);
            if (match_char(lex, '=')) return make_token(lex, TOK_AND_ASSIGN);
            return make_token(lex, TOK_BIT_AND);

        case '|':
            if (match_char(lex, '|')) return make_token(lex, TOK_OR);
            if (match_char(lex, '=')) return make_token(lex, TOK_OR_ASSIGN);
            return make_token(lex, TOK_BIT_OR);

        case '^':
            if (match_char(lex, '=')) return make_token(lex, TOK_XOR_ASSIGN);
            return make_token(lex, TOK_BIT_XOR);

        case ':':
            if (match_char(lex, '=')) return make_token(lex, TOK_DERIVE);
            return make_token(lex, TOK_COLON);

        case '#':
            // Preprocessor directive (simplified: rest of line as raw text)
            return scan_preprocessor(lex);

        // Literals
        case '"': return scan_string(lex, '"');
        case '\'': return scan_string(lex, '\'');

        default:
            if (isdigit((unsigned char)c)) {
                lex->current--; lex->offset--; lex->col--;  // put back digit
                return scan_number(lex);
            }
            if (isalpha((unsigned char)c) || c == '_') {
                lex->current--; lex->offset--; lex->col--;  // put back first char
                return scan_identifier(lex);
            }
            break;
    }

    return make_error_token(lex, "unexpected character");
}

static Token scan_identifier(Lexer* lex) {
    while (!is_at_end(lex) && (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')) {
        advance(lex);
    }
    // Check if it's a keyword
    const char* start = lex->start;
    size_t len = (size_t)(lex->current - lex->start);
    Token t = check_keyword(lex, start, len);
    if (t.kind != TOK_IDENTIFIER) return t;
    return make_token(lex, TOK_IDENTIFIER);
}

static Token check_keyword(Lexer* lex, const char* start, size_t len) {
    for (size_t i = 0; i < NUM_KEYWORDS; i++) {
        size_t kw_len = strlen(keywords[i].word);
        if (len == kw_len && memcmp(start, keywords[i].word, kw_len) == 0) {
            return make_token(lex, keywords[i].kind);
        }
    }
    return make_token(lex, TOK_IDENTIFIER);
}

static Token scan_number(Lexer* lex) {
    int is_hex = 0;
    int is_float = 0;

    // Check for hex prefix
    if (peek(lex) == '0' && (peek_next(lex) == 'x' || peek_next(lex) == 'X')) {
        is_hex = 1;
        advance(lex); // '0'
        advance(lex); // 'x'
        while (!is_at_end(lex) && isxdigit((unsigned char)peek(lex))) {
            advance(lex);
        }
    } else {
        while (!is_at_end(lex) && isdigit((unsigned char)peek(lex))) {
            advance(lex);
        }
    }

    // Check for float (dot or exponent)
    if (!is_hex && peek(lex) == '.' && !is_at_end(lex) && isdigit((unsigned char)peek_next(lex))) {
        is_float = 1;
        advance(lex); // '.'
        while (!is_at_end(lex) && isdigit((unsigned char)peek(lex))) {
            advance(lex);
        }
    }

    if (peek(lex) == 'e' || peek(lex) == 'E' || peek(lex) == 'p' || peek(lex) == 'P') {
        is_float = 1;
        advance(lex);
        if (peek(lex) == '+' || peek(lex) == '-') advance(lex);
        while (!is_at_end(lex) && isdigit((unsigned char)peek(lex))) {
            advance(lex);
        }
    }

    // Float suffix
    if (peek(lex) == 'f' || peek(lex) == 'F' || peek(lex) == 'l' || peek(lex) == 'L') {
        is_float = 1;
        advance(lex);
    }

    // Integer suffix
    if (!is_float) {
        while (peek(lex) == 'u' || peek(lex) == 'U' || peek(lex) == 'l' || peek(lex) == 'L') {
            advance(lex);
        }
    }

    Token t = make_token(lex, is_float ? TOK_FLOAT_LITERAL : TOK_INT_LITERAL);

    // Parse the value
    char* end = NULL;
    if (is_float) {
        t.value.f64 = strtod(t.text, &end);
    } else {
        if (is_hex) {
            t.value.i64 = strtoll(t.text, &end, 16);
        } else {
            t.value.i64 = strtoll(t.text, &end, 10);
        }
    }

    return t;
}

static Token scan_string(Lexer* lex, int delimiter) {
    while (!is_at_end(lex)) {
        char c = advance(lex);
        if (c == '\\') {
            if (!is_at_end(lex)) advance(lex); // skip escaped char
        } else if (c == delimiter) {
            return make_token(lex, delimiter == '"' ? TOK_STRING_LITERAL : TOK_CHAR_LITERAL);
        } else if (c == '\n') {
            lex->line++;
            lex->col = 1;
        }
    }
    return make_error_token(lex, "unterminated string");
}

// Simplified preprocessor handling: treat the whole directive line as a raw token
static Token scan_preprocessor(Lexer* lex) {
    // Skip whitespace after #
    while (peek(lex) == ' ' || peek(lex) == '\t') advance(lex);

    // Identify the directive keyword
    const char* dir_start = lex->current;
    while (!is_at_end(lex) && isalpha((unsigned char)peek(lex))) advance(lex);
    size_t dir_len = (size_t)(lex->current - dir_start);

    TokenKind kind = TOK_PP_DIRECTIVE;
    if (dir_len == 7 && memcmp(dir_start, "include", 7) == 0) kind = TOK_PP_INCLUDE;
    else if (dir_len == 6 && memcmp(dir_start, "define", 6) == 0) kind = TOK_PP_DEFINE;
    else if (dir_len == 5 && memcmp(dir_start, "ifdef", 5) == 0) kind = TOK_PP_IFDEF;
    else if (dir_len == 6 && memcmp(dir_start, "ifndef", 6) == 0) kind = TOK_PP_IFNDEF;
    else if (dir_len == 5 && memcmp(dir_start, "endif", 5) == 0) kind = TOK_PP_ENDIF;
    else if (dir_len == 4 && memcmp(dir_start, "else", 4) == 0) kind = TOK_PP_ELSE;
    else if (dir_len == 2 && memcmp(dir_start, "if", 2) == 0) kind = TOK_PP_IF;
    else if (dir_len == 4 && memcmp(dir_start, "line", 4) == 0) kind = TOK_PP_LINE;
    else if (dir_len == 5 && memcmp(dir_start, "pragma", 5) == 0) kind = TOK_PP_PRAGMA;

    // Consume rest of line
    while (!is_at_end(lex) && peek(lex) != '\n') advance(lex);

    return make_token(lex, kind);
}

// ── Token name lookup ───────────────────────────────────────────────────

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_ERROR: return "error";
        case TOK_BORROW: return "borrow";
        case TOK_OWN: return "own";
        case TOK_WHEN: return "when";
        case TOK_NO_DERIVE: return "no_derive";
        case TOK_DERIVE: return ":=";
        case TOK_DBL_OPEN: return "[[";
        case TOK_DBL_CLOSE: return "]]";
        case TOK_IF: return "if";
        case TOK_ELSE: return "else";
        case TOK_SWITCH: return "switch";
        case TOK_CASE: return "case";
        case TOK_DEFAULT: return "default";
        case TOK_WHILE: return "while";
        case TOK_DO: return "do";
        case TOK_FOR: return "for";
        case TOK_RETURN: return "return";
        case TOK_BREAK: return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_GOTO: return "goto";
        case TOK_SIZEOF: return "sizeof";
        case TOK_TYPEDEF: return "typedef";
        case TOK_EXTERN: return "extern";
        case TOK_STATIC: return "static";
        case TOK_CONST: return "const";
        case TOK_VOLATILE: return "volatile";
        case TOK_RESTRICT: return "restrict";
        case TOK_STRUCT: return "struct";
        case TOK_UNION: return "union";
        case TOK_ENUM: return "enum";
        case TOK_VOID: return "void";
        case TOK_CHAR: return "char";
        case TOK_SHORT: return "short";
        case TOK_INT: return "int";
        case TOK_LONG: return "long";
        case TOK_FLOAT: return "float";
        case TOK_DOUBLE: return "double";
        case TOK_SIGNED: return "signed";
        case TOK_UNSIGNED: return "unsigned";
        case TOK_BOOL: return "_Bool";
        case TOK_COMPLEX: return "_Complex";
        case TOK_IMAGINARY: return "_Imaginary";
        case TOK_INLINE: return "inline";
        case TOK_NORETURN: return "_Noreturn";
        case TOK_ATOMIC: return "_Atomic";
        case TOK_REGISTER: return "register";
        case TOK_AUTO: return "auto";
        case TOK_LBRACE: return "{";
        case TOK_RBRACE: return "}";
        case TOK_LPAREN: return "(";
        case TOK_RPAREN: return ")";
        case TOK_LBRACK: return "[";
        case TOK_RBRACK: return "]";
        case TOK_SEMI: return ";";
        case TOK_COMMA: return ",";
        case TOK_DOT: return ".";
        case TOK_ARROW: return "->";
        case TOK_COLON: return ":";
        case TOK_ASSIGN: return "=";
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_DIV: return "/";
        case TOK_MOD: return "%";
        case TOK_INC: return "++";
        case TOK_DEC: return "--";
        case TOK_EQ: return "==";
        case TOK_NE: return "!=";
        case TOK_LT: return "<";
        case TOK_GT: return ">";
        case TOK_LE: return "<=";
        case TOK_GE: return ">=";
        case TOK_AND: return "&&";
        case TOK_OR: return "||";
        case TOK_NOT: return "!";
        case TOK_BIT_AND: return "&";
        case TOK_BIT_OR: return "|";
        case TOK_BIT_XOR: return "^";
        case TOK_BIT_NOT: return "~";
        case TOK_SHL: return "<<";
        case TOK_SHR: return ">>";
        case TOK_QUESTION: return "?";
        case TOK_HASH: return "#";
        case TOK_IDENTIFIER: return "identifier";
        case TOK_INT_LITERAL: return "integer literal";
        case TOK_FLOAT_LITERAL: return "float literal";
        case TOK_STRING_LITERAL: return "string literal";
        case TOK_CHAR_LITERAL: return "char literal";
        case TOK_ADD_ASSIGN: return "+=";
        case TOK_SUB_ASSIGN: return "-=";
        case TOK_MUL_ASSIGN: return "*=";
        case TOK_DIV_ASSIGN: return "/=";
        case TOK_MOD_ASSIGN: return "%=";
        case TOK_AND_ASSIGN: return "&=";
        case TOK_OR_ASSIGN: return "|=";
        case TOK_XOR_ASSIGN: return "^=";
        case TOK_SHL_ASSIGN: return "<<=";
        case TOK_SHR_ASSIGN: return ">>=";
        case TOK_PP_INCLUDE: return "#include";
        case TOK_PP_DEFINE: return "#define";
        case TOK_PP_IFDEF: return "#ifdef";
        case TOK_PP_IFNDEF: return "#ifndef";
        case TOK_PP_ENDIF: return "#endif";
        case TOK_PP_ELSE: return "#else";
        case TOK_PP_IF: return "#if";
        case TOK_PP_LINE: return "#line";
        case TOK_PP_PRAGMA: return "#pragma";
        case TOK_PP_DIRECTIVE: return "preprocessor directive";
    }
    return "unknown";
}

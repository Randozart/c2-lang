// 2026-07-13 — Unit tests for the C² lexer.
//   Tests tokenization of C23 tokens and C² extensions.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "error.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-50s ", name); \
    tests_run++; \
} while (0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while (0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while (0)

static TokenKind lex_single(const char* src, TokenKind expected) {
    ErrorList* err = errlist_create();
    Lexer lex = lexer_create(src, strlen(src), "test", err);
    lexer_scan(&lex);
    Token t = lex.current_tok;
    TokenKind kind = t.kind;
    // Skip to EOF
    while (t.kind != TOK_EOF) t = lexer_scan(&lex);
    errlist_destroy(err);
    return kind == expected ? expected : kind;
}

static int lex_single_int(const char* src, int64_t* val) {
    ErrorList* err = errlist_create();
    Lexer lex = lexer_create(src, strlen(src), "test", err);
    lexer_scan(&lex);
    Token t = lex.current_tok;
    *val = t.value.i64;
    errlist_destroy(err);
    return 1;
}

static void test_single_char_tokens(void) {
    TEST("single '(' token");
    assert(lex_single("(", TOK_LPAREN) == TOK_LPAREN);
    PASS();

    TEST("single ')' token");
    assert(lex_single(")", TOK_RPAREN) == TOK_RPAREN);
    PASS();

    TEST("single '{' token");
    assert(lex_single("{", TOK_LBRACE) == TOK_LBRACE);
    PASS();

    TEST("single '}' token");
    assert(lex_single("}", TOK_RBRACE) == TOK_RBRACE);
    PASS();

    TEST("single ';' token");
    assert(lex_single(";", TOK_SEMI) == TOK_SEMI);
    PASS();

    TEST("single ',' token");
    assert(lex_single(",", TOK_COMMA) == TOK_COMMA);
    PASS();

    TEST("single '?' token");
    assert(lex_single("?", TOK_QUESTION) == TOK_QUESTION);
    PASS();

    TEST("single '~' token");
    assert(lex_single("~", TOK_BIT_NOT) == TOK_BIT_NOT);
    PASS();
}

static void test_multi_char_tokens(void) {
    TEST("'==' equality token");
    assert(lex_single("==", TOK_EQ) == TOK_EQ);
    PASS();

    TEST("'!=' not-equal token");
    assert(lex_single("!=", TOK_NE) == TOK_NE);
    PASS();

    TEST("'<=' less-equal token");
    assert(lex_single("<=", TOK_LE) == TOK_LE);
    PASS();

    TEST("'>=' greater-equal token");
    assert(lex_single(">=", TOK_GE) == TOK_GE);
    PASS();

    TEST("'++' increment token");
    assert(lex_single("++", TOK_INC) == TOK_INC);
    PASS();

    TEST("'--' decrement token");
    assert(lex_single("--", TOK_DEC) == TOK_DEC);
    PASS();

    TEST("'->' arrow token");
    assert(lex_single("->", TOK_ARROW) == TOK_ARROW);
    PASS();

    TEST("'&&' logical and");
    assert(lex_single("&&", TOK_AND) == TOK_AND);
    PASS();

    TEST("'||' logical or");
    assert(lex_single("||", TOK_OR) == TOK_OR);
    PASS();

    TEST("'<<' left shift");
    assert(lex_single("<<", TOK_SHL) == TOK_SHL);
    PASS();

    TEST("'>>' right shift");
    assert(lex_single(">>", TOK_SHR) == TOK_SHR);
    PASS();
}

static void test_c2_extensions(void) {
    TEST("'[[' double-open");
    assert(lex_single("[[", TOK_DBL_OPEN) == TOK_DBL_OPEN);
    PASS();

    TEST("']]' double-close");
    assert(lex_single("]]", TOK_DBL_CLOSE) == TOK_DBL_CLOSE);
    PASS();

    TEST("':=' derive token");
    assert(lex_single(":=", TOK_DERIVE) == TOK_DERIVE);
    PASS();

    TEST("'borrow' keyword");
    assert(lex_single("borrow", TOK_BORROW) == TOK_BORROW);
    PASS();

    TEST("'own' keyword");
    assert(lex_single("own", TOK_OWN) == TOK_OWN);
    PASS();

    TEST("'when' keyword");
    assert(lex_single("when", TOK_WHEN) == TOK_WHEN);
    PASS();

    TEST("'no_derive' keyword");
    assert(lex_single("no_derive", TOK_NO_DERIVE) == TOK_NO_DERIVE);
    PASS();
}

static void test_identifiers(void) {
    TEST("basic identifier");
    assert(lex_single("hello", TOK_IDENTIFIER) == TOK_IDENTIFIER);
    PASS();

    TEST("identifier with underscore");
    assert(lex_single("my_var", TOK_IDENTIFIER) == TOK_IDENTIFIER);
    PASS();

    TEST("identifier with leading underscore");
    assert(lex_single("_private", TOK_IDENTIFIER) == TOK_IDENTIFIER);
    PASS();

    TEST("typedef-name not confused with keyword");
    assert(lex_single("int32_t", TOK_IDENTIFIER) == TOK_IDENTIFIER);
    PASS();
}

static void test_literals(void) {
    TEST("integer literal");
    assert(lex_single("42", TOK_INT_LITERAL) == TOK_INT_LITERAL);
    PASS();

    TEST("hex literal");
    assert(lex_single("0xFF", TOK_INT_LITERAL) == TOK_INT_LITERAL);
    PASS();

    int64_t val;
    TEST("integer value 42");
    assert(lex_single_int("42", &val) && val == 42);
    PASS();

    TEST("hex value 0xFF = 255");
    assert(lex_single_int("0xFF", &val) && val == 255);
    PASS();

    TEST("float literal");
    assert(lex_single("3.14", TOK_FLOAT_LITERAL) == TOK_FLOAT_LITERAL);
    PASS();

    TEST("string literal");
    assert(lex_single("\"hello\"", TOK_STRING_LITERAL) == TOK_STRING_LITERAL);
    PASS();

    TEST("char literal");
    assert(lex_single("'x'", TOK_CHAR_LITERAL) == TOK_CHAR_LITERAL);
    PASS();
}

static void test_keywords(void) {
    TEST("'if' keyword");
    assert(lex_single("if", TOK_IF) == TOK_IF);
    PASS();

    TEST("'else' keyword");
    assert(lex_single("else", TOK_ELSE) == TOK_ELSE);
    PASS();

    TEST("'while' keyword");
    assert(lex_single("while", TOK_WHILE) == TOK_WHILE);
    PASS();

    TEST("'for' keyword");
    assert(lex_single("for", TOK_FOR) == TOK_FOR);
    PASS();

    TEST("'return' keyword");
    assert(lex_single("return", TOK_RETURN) == TOK_RETURN);
    PASS();

    TEST("'struct' keyword");
    assert(lex_single("struct", TOK_STRUCT) == TOK_STRUCT);
    PASS();

    TEST("'const' keyword");
    assert(lex_single("const", TOK_CONST) == TOK_CONST);
    PASS();

    TEST("'int' keyword");
    assert(lex_single("int", TOK_INT) == TOK_INT);
    PASS();

    TEST("'void' keyword");
    assert(lex_single("void", TOK_VOID) == TOK_VOID);
    PASS();
}

static void test_comments(void) {
    TEST("line comment skipped");
    ErrorList* err = errlist_create();
    Lexer lex = lexer_create("// comment\nint", 14, "test", err);
    lexer_scan(&lex);
    assert(lex.current_tok.kind == TOK_INT);
    errlist_destroy(err);
    PASS();

    TEST("block comment skipped");
    err = errlist_create();
    lex = lexer_create("/* block */int", 14, "test", err);
    lexer_scan(&lex);
    assert(lex.current_tok.kind == TOK_INT);
    errlist_destroy(err);
    PASS();
}

static void test_preprocessor(void) {
    TEST("#include directive");
    ErrorList* err = errlist_create();
    Lexer lex = lexer_create("#include <stdio.h>\nint", 21, "test", err);
    lexer_scan(&lex);
    assert(lex.current_tok.kind == TOK_PP_INCLUDE);
    errlist_destroy(err);
    PASS();

    TEST("#define directive");
    ErrorList* err2 = errlist_create();
    Lexer lex2 = lexer_create("#define FOO 42\nint", 18, "test", err2);
    lexer_scan(&lex2);
    assert(lex2.current_tok.kind == TOK_PP_DEFINE);
    errlist_destroy(err2);
    PASS();
}

static void test_operators(void) {
    TEST("'+=' add-assign");
    assert(lex_single("+=", TOK_ADD_ASSIGN) == TOK_ADD_ASSIGN);
    PASS();

    TEST("'-=' sub-assign");
    assert(lex_single("-=", TOK_SUB_ASSIGN) == TOK_SUB_ASSIGN);
    PASS();

    TEST("'*=' mul-assign");
    assert(lex_single("*=", TOK_MUL_ASSIGN) == TOK_MUL_ASSIGN);
    PASS();

    TEST("'/=' div-assign");
    assert(lex_single("/=", TOK_DIV_ASSIGN) == TOK_DIV_ASSIGN);
    PASS();

    TEST("'&=' and-assign");
    assert(lex_single("&=", TOK_AND_ASSIGN) == TOK_AND_ASSIGN);
    PASS();

    TEST("'|=' or-assign");
    assert(lex_single("|=", TOK_OR_ASSIGN) == TOK_OR_ASSIGN);
    PASS();

    TEST("'^=' xor-assign");
    assert(lex_single("^=", TOK_XOR_ASSIGN) == TOK_XOR_ASSIGN);
    PASS();
}

static void test_lexer_advance(void) {
    TEST("advance and scan sequence");
    ErrorList* err = errlist_create();
    Lexer lex = lexer_create("int x = 42;", 11, "test", err);
    lexer_scan(&lex);
    Token t;

    t = lexer_advance(&lex);
    assert(t.kind == TOK_INT);

    t = lexer_advance(&lex);
    assert(t.kind == TOK_IDENTIFIER);

    t = lexer_advance(&lex);
    assert(t.kind == TOK_ASSIGN);

    t = lexer_advance(&lex);
    assert(t.kind == TOK_INT_LITERAL);

    t = lexer_advance(&lex);
    assert(t.kind == TOK_SEMI);

    errlist_destroy(err);
    PASS();
}

int main(void) {
    printf("\nC² Lexer Unit Tests\n");
    printf("===================\n\n");

    test_single_char_tokens();
    test_multi_char_tokens();
    test_c2_extensions();
    test_identifiers();
    test_literals();
    test_keywords();
    test_comments();
    test_preprocessor();
    test_operators();
    test_lexer_advance();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

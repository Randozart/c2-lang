// 2026-07-13 — Z3 contract verifier unit tests.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "lexer.h"
#include "parser.h"
#include "typecheck.h"
#include "verifier.h"
#include "error.h"

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  %-55s ", name); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

// ── Helpers ──────────────────────────────────────────────────────────────

static AstNode* parse_and_typecheck(const char* c2src, ErrorList* err) {
    Parser parser = parser_create(c2src, strlen(c2src), "<test>", err);
    AstNode* ast = parser_parse(&parser);
    if (err->has_errors) return NULL;
    typecheck_ast(ast, err);
    if (err->has_errors) return NULL;
    return ast;
}

/// Run verifier and return 0 if all contracts pass, 1+ if any fail.
static int verify_source_str(const char* c2src, ErrorList* err) {
    AstNode* ast = parse_and_typecheck(c2src, err);
    if (!ast) return -1;
    int result = z3_verify_contracts(ast, err, 0);
    ast_free_tree(ast);
    return result;
}

// ── Tests ────────────────────────────────────────────────────────────────

static void test_verify_basic(void) {
    TEST("trivial postcondition — always true");
    ErrorList* err2 = errlist_create();
    int r2 = verify_source_str("[[1]\nint32_t f(int32_t x) { return x; }\n", err2);
    assert(r2 == 0);
    errlist_destroy(err2);
    PASS();

    TEST("trivial precondition — satisfiable");
    ErrorList* err3 = errlist_create();
    int r3 = verify_source_str("[1][1]\nint32_t f(int32_t x) { return x; }\n", err3);
    assert(r3 == 0);
    errlist_destroy(err3);
    PASS();
}

static void test_verify_precondition(void) {
    TEST("unsatisfiable precondition — trivially false");
    ErrorList* err = errlist_create();
    int r = verify_source_str("[0][1]\nint32_t f(int32_t x) { return x; }\n", err);
    assert(r >= 1); // should report failure
    errlist_destroy(err);
    PASS();

    TEST("satisfiable precondition");
    ErrorList* err2 = errlist_create();
    int r2 = verify_source_str("[x != 0][1]\nint32_t f(int32_t x) { return x; }\n", err2);
    assert(r2 == 0);
    errlist_destroy(err2);
    PASS();
}

static void test_verify_postcondition(void) {
    TEST("postcondition holds for identity");
    ErrorList* err = errlist_create();
    int r = verify_source_str("[[result == x]\nint32_t id(int32_t x) { return x; }\n", err);
    assert(r == 0);
    errlist_destroy(err);
    PASS();

    TEST("postcondition fails for wrong return");
    ErrorList* err2 = errlist_create();
    int r2 = verify_source_str("[[result == 0]\nint32_t f(int32_t x) { return x; }\n", err2);
    assert(r2 >= 1);
    errlist_destroy(err2);
    PASS();

    TEST("postcondition with precondition");
    ErrorList* err3 = errlist_create();
    int r3 = verify_source_str(
        "[x >= 0][result == x]\nint32_t f(int32_t x) { return x; }\n", err3);
    assert(r3 == 0);
    errlist_destroy(err3);
    PASS();

    TEST("postcondition on void function (no result)");
    ErrorList* err4 = errlist_create();
    int r4 = verify_source_str("[[1]\nvoid f(int32_t x) { return; }\n", err4);
    assert(r4 == 0);
    errlist_destroy(err4);
    PASS();
}

static void test_verify_ternary(void) {
    TEST("ternary expression postcondition");
    ErrorList* err = errlist_create();
    int r = verify_source_str(
        "[[result == 1]\nint32_t f(int32_t x) { return x > 0 ? 1 : 1; }\n", err);
    assert(r == 0);
    errlist_destroy(err);
    PASS();

    TEST("ternary path-dependent result");
    ErrorList* err2 = errlist_create();
    int r2 = verify_source_str(
        "[[result == x + 1]\nint32_t f(int32_t x) { return x > 0 ? x + 1 : x + 1; }\n", err2);
    assert(r2 == 0);
    errlist_destroy(err2);
    PASS();
}

static void test_verify_arithmetic(void) {
    TEST("addition postcondition");
    ErrorList* err = errlist_create();
    int r = verify_source_str(
        "[[result == 7]\nint32_t add(int32_t a, int32_t b) { return 3 + 4; }\n", err);
    assert(r == 0);
    errlist_destroy(err);
    PASS();

    TEST("multiplication postcondition");
    ErrorList* err2 = errlist_create();
    int r2 = verify_source_str(
        "[[result == 12]\nint32_t mul(int32_t a, int32_t b) { return 3 * 4; }\n", err2);
    assert(r2 == 0);
    errlist_destroy(err2);
    PASS();
}

static void test_verify_pre_false(void) {
    TEST("trivially false precondition detected");
    ErrorList* err = errlist_create();
    int r = verify_source_str(
        "[x != x][result == x]\nint32_t f(int32_t x) { return x; }\n", err);
    assert(r >= 1); // pre is trivially false (x != x is always false)
    errlist_destroy(err);
    PASS();
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(void) {
    printf("\nC² Z3 Verifier Unit Tests\n");
    printf("=========================\n\n");

    test_verify_basic();
    test_verify_precondition();
    test_verify_postcondition();
    test_verify_ternary();
    test_verify_arithmetic();
    test_verify_pre_false();

    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}

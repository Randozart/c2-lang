// 2026-07-13 — Unit tests for the C² type checker.
//   Tests type inference and validation for expressions, assignments,
//   function calls, return types, contracts, and C²-specific constructs.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "type.h"
#include "typecheck.h"
#include "codegen.h"
#include "error.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-50s  ", name); fflush(stdout); \
    tests_run++; \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

static const char* transpile(const char* src, ErrorList* err) {
    Parser parser = parser_create(src, strlen(src), "<test>", err);
    AstNode* ast = parser_parse(&parser);
    if (err->has_errors) return NULL;

    typecheck_ast(ast, err);
    if (err->has_errors) return NULL;

    Codegen cg;
    codegen_init(&cg, err);
    codegen_generate(&cg, ast);
    const char* out = strdup(cg.output);
    codegen_free(&cg);
    ast_free_tree(ast);
    return out;
}

// ── Type system tests ───────────────────────────────────────────────────

static void test_type_construction(void) {
    TEST("create primitive types");
    Type* t1 = type_primitive(TYPE_INT32);
    assert(t1 != NULL);
    assert(t1->kind == TYPE_INT32);
    assert(t1->bit_width == 32);
    assert(t1->is_signed == 1);
    type_free(t1);

    Type* t2 = type_primitive(TYPE_UINT8);
    assert(t2 != NULL);
    assert(t2->kind == TYPE_UINT8);
    assert(t2->bit_width == 8);
    assert(t2->is_signed == 0);
    type_free(t2);
    PASS();

    TEST("create pointer type");
    Type* base = type_primitive(TYPE_INT32);
    Type* ptr = type_pointer(base);
    assert(ptr->kind == TYPE_POINTER);
    assert(ptr->subtype->kind == TYPE_INT32);
    type_free(ptr);
    PASS();

    TEST("create array type");
    Type* arr = type_array(type_primitive(TYPE_INT32), 10);
    assert(arr->kind == TYPE_ARRAY);
    assert(arr->array_size == 10);
    assert(arr->subtype->kind == TYPE_INT32);
    type_free(arr);
    PASS();

    TEST("create function type");
    Type* ret = type_primitive(TYPE_INT32);
    Type* params[2] = { type_primitive(TYPE_INT32), type_primitive(TYPE_DOUBLE) };
    Type* func = type_function(ret, params, 2);
    assert(func->kind == TYPE_FUNCTION);
    assert(func->subtype->kind == TYPE_INT32);
    assert(func->param_count == 2);
    assert(func->param_types[0]->kind == TYPE_INT32);
    assert(func->param_types[1]->kind == TYPE_DOUBLE);
    type_free(func);
    PASS();

    TEST("type queries");
    assert(type_is_integer(type_primitive(TYPE_INT32)));
    assert(type_is_integer(type_primitive(TYPE_UINT64)));
    assert(!type_is_integer(type_primitive(TYPE_DOUBLE)));
    assert(type_is_floating(type_primitive(TYPE_DOUBLE)));
    assert(type_is_arithmetic(type_primitive(TYPE_INT32)));
    assert(type_is_arithmetic(type_primitive(TYPE_FLOAT)));
    assert(type_is_scalar(type_primitive(TYPE_INT32)));
    assert(type_is_scalar(type_pointer(type_primitive(TYPE_VOID))));
    assert(!type_is_scalar(type_primitive(TYPE_VOID)));
    assert(type_is_pointer(type_pointer(type_primitive(TYPE_INT32))));
    assert(!type_is_pointer(type_primitive(TYPE_INT32)));
    assert(type_is_void(type_primitive(TYPE_VOID)));
    assert(type_is_bool(type_primitive(TYPE_BOOL)));
    assert(type_is_signed(type_primitive(TYPE_INT32)));
    assert(type_is_unsigned(type_primitive(TYPE_UINT32)));
    PASS();

    TEST("type equality");
    assert(type_equal(type_primitive(TYPE_INT32), type_primitive(TYPE_INT32)));
    assert(!type_equal(type_primitive(TYPE_INT32), type_primitive(TYPE_INT64)));
    assert(!type_equal(type_primitive(TYPE_INT32), type_primitive(TYPE_UINT32)));
    Type* p1 = type_pointer(type_primitive(TYPE_INT32));
    Type* p2 = type_pointer(type_primitive(TYPE_INT32));
    assert(type_equal(p1, p2));
    Type* p3 = type_pointer(type_primitive(TYPE_DOUBLE));
    assert(!type_equal(p1, p3));
    type_free(p1); type_free(p2); type_free(p3);
    PASS();

    TEST("type assignable");
    assert(type_assignable(type_primitive(TYPE_INT32), type_primitive(TYPE_INT32)));
    assert(type_assignable(type_primitive(TYPE_INT64), type_primitive(TYPE_INT32)));
    assert(type_assignable(type_primitive(TYPE_DOUBLE), type_primitive(TYPE_FLOAT)));
    assert(type_assignable(type_primitive(TYPE_FLOAT), type_primitive(TYPE_INT32)));
    Type* vp = type_pointer(type_primitive(TYPE_VOID));
    Type* ip = type_pointer(type_primitive(TYPE_INT32));
    assert(type_assignable(vp, ip));
    assert(type_assignable(ip, vp));
    type_free(vp); type_free(ip);
    PASS();

    TEST("type formatting");
    char buf[128];
    type_to_string(type_primitive(TYPE_INT32), buf, sizeof(buf));
    assert(strcmp(buf, "int32_t") == 0);
    type_to_string(type_primitive(TYPE_DOUBLE), buf, sizeof(buf));
    assert(strcmp(buf, "double") == 0);
    type_to_string(type_pointer(type_primitive(TYPE_INT32)), buf, sizeof(buf));
    assert(strcmp(buf, "int32_t*") == 0);
    type_to_string(type_array(type_primitive(TYPE_INT32), 16), buf, sizeof(buf));
    assert(strcmp(buf, "int32_t[16]") == 0);
    PASS();
}

// ── Type checker integration tests ───────────────────────────────────────

static void test_tc_literals(void) {
    TEST("infer int literal type");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nint32_t f() { return 42; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "42") != NULL);
    errlist_destroy(err);
    PASS();

    TEST("infer float literal type");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\ndouble f() { return 3.14; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    errlist_destroy(err2);
    PASS();

    TEST("infer string literal type");
    ErrorList* err3 = errlist_create();
    const char* src3 = "[1][1]\nconst char* f() { return \"hello\"; }\n";
    const char* out3 = transpile(src3, err3);
    assert(out3 != NULL);
    errlist_destroy(err3);
    PASS();
}

static void test_tc_return_type(void) {
    TEST("valid return type");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nint32_t f() { return 42; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("return type mismatch");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\nint32_t f() { return 3.14; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    errlist_destroy(err2);
    PASS();

    TEST("void function returning value");
    ErrorList* err3 = errlist_create();
    const char* src3 = "[1][1]\nvoid f() { return 42; }\n";
    const char* out3 = transpile(src3, err3);
    assert(out3 == NULL);
    assert(err3->has_errors);
    errlist_destroy(err3);
    PASS();

    TEST("non-void function no return");
    ErrorList* err4 = errlist_create();
    const char* src4 = "[1][1]\nint32_t f() { return; }\n";
    const char* out4 = transpile(src4, err4);
    assert(out4 == NULL);
    assert(err4->has_errors);
    errlist_destroy(err4);
    PASS();
}

static void test_tc_function_call(void) {
    TEST("valid function call");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nint32_t g(int32_t a) { return a; }\n"
                      "[1][1]\nint32_t f() { return g(42); }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("function call wrong arg count");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\nint32_t g(int32_t a, double b) { return a; }\n"
                       "[1][1]\nint32_t f() { return g(42); }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 == NULL);
    assert(err2->has_errors);
    errlist_destroy(err2);
    PASS();

    TEST("function call wrong arg type");
    ErrorList* err3 = errlist_create();
    const char* src3 = "[1][1]\nint32_t g(double a) { return (int32_t)a; }\n"
                       "[1][1]\nint32_t f() { return g(\"hello\"); }\n";
    const char* out3 = transpile(src3, err3);
    assert(out3 == NULL);
    assert(err3->has_errors);
    errlist_destroy(err3);
    PASS();
}

static void test_tc_assignment(void) {
    TEST("valid assignment");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nint32_t f() { int32_t x; x = 42; return x; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("valid pointer assignment");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\nvoid f() { int32_t* p; int32_t x; p = &x; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    errlist_destroy(err2);
    PASS();
}

static void test_tc_contracts(void) {
    TEST("valid contract expressions");
    ErrorList* err = errlist_create();
    const char* src = "[x > 0][x < 100]\nint32_t f(int32_t x) { return x; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("result in postcondition");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][result > 0]\nint32_t f(int32_t x) { return x; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    errlist_destroy(err2);
    PASS();

    TEST("when condition must be scalar");
    ErrorList* err3 = errlist_create();
    const char* src3 = "[1][1]\nvoid f() { when 1 -> {}; }\n";
    const char* out3 = transpile(src3, err3);
    assert(out3 != NULL);
    errlist_destroy(err3);
    PASS();
}

static void test_tc_derivation(void) {
    TEST("valid derivation example count");
    ErrorList* err = errlist_create();
    const char* src = "int32_t add(int32_t a, int32_t b) := {\n"
                      "    1, 2 -> 3;\n"
                      "    10, 20 -> 30;\n"
                      "} { return a + b; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("derivation example count mismatch");
    ErrorList* err2 = errlist_create();
    const char* src2 = "int32_t add(int32_t a, int32_t b) := {\n"
                       "    1 -> 1;\n"
                       "} { return a + b; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 == NULL);
    assert(err2->has_errors);
    errlist_destroy(err2);
    PASS();
}

static void test_tc_borrow_own(void) {
    TEST("borrow on pointer param");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nvoid f(borrow int32_t* p) { *p = 42; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("borrow on non-pointer (error)");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\nvoid f(borrow int32_t x) { }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 == NULL);
    assert(err2->has_errors);
    errlist_destroy(err2);
    PASS();
}

static void test_tc_clean_source(void) {
    ErrorList* err = errlist_create();
    const char* src = "[a > 0][a > 0]\nint32_t add(int32_t a, int32_t b) {\n"
                      "    return a + b;\n"
                      "}\n"
                      "[1][1]\nint32_t main() {\n"
                      "    int32_t x = 10;\n"
                      "    int32_t y = 20;\n"
                      "    return add(x, y);\n"
                      "}\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    assert(strstr(out, "int32_t add(int32_t a, int32_t b)") != NULL);
    errlist_destroy(err);
    PASS();
}

static void test_tc_binary_expr(void) {
    TEST("arithmetic binary expression");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nint32_t f(int32_t a, int32_t b) { return a + b * 3; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("comparison expression");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\nint32_t f(int32_t a) { when a > 0 -> return a; return -1; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    errlist_destroy(err2);
    PASS();
}

static void test_tc_edge_cases(void) {
    TEST("array index integer check");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\nint32_t f() { int32_t arr[10]; return arr[0]; }\n";
    const char* out = transpile(src, err);
    assert(out != NULL);
    errlist_destroy(err);
    PASS();

    TEST("ternary expression");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\nint32_t f(int32_t x) { return x > 0 ? x : -x; }\n";
    const char* out2 = transpile(src2, err2);
    assert(out2 != NULL);
    errlist_destroy(err2);
    PASS();

    TEST("unary operators");
    ErrorList* err3 = errlist_create();
    const char* src3 = "[1][1]\nint32_t f(int32_t x) { return -x + !x; }\n";
    const char* out3 = transpile(src3, err3);
    assert(out3 != NULL);
    errlist_destroy(err3);
    PASS();
}

int main(void) {
    printf("\nC² Type Checker Unit Tests\n");
    printf("==========================\n\n");

    test_type_construction();
    test_tc_literals();
    test_tc_return_type();
    test_tc_function_call();
    test_tc_assignment();
    test_tc_contracts();
    test_tc_derivation();
    test_tc_borrow_own();
    test_tc_clean_source();
    test_tc_binary_expr();
    test_tc_edge_cases();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

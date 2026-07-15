// 2026-07-13 — Unit tests for the C² parser.
//   Tests parsing of C23 and C² constructs into AST nodes.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "ast.h"
#include "parser.h"
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

static AstNode* parse_source(const char* src, ErrorList* err) {
    Parser parser = parser_create(src, strlen(src), "test", err);
    return parser_parse(&parser);
}

static void test_empty_file(void) {
    TEST("parse empty file");
    ErrorList* err = errlist_create();
    AstNode* root = parse_source("", err);
    assert(root != NULL);
    assert(root->kind == NODE_TRANSLATION_UNIT);
    assert(root->child_count == 0);
    assert(errlist_count(err) == 0);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_simple_function(void) {
    TEST("parse function with contract");
    ErrorList* err = errlist_create();
    const char* src = "[x > 0][result == x + 1]\n"
                      "int32_t increment(int32_t x) {\n"
                      "    return x + 1;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL);
    assert(root->kind == NODE_TRANSLATION_UNIT);
    assert(root->child_count == 1);
    AstNode* func = root->children[0];
    assert(func->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_func_no_contract(void) {
    TEST("parse function missing contract emits error");
    ErrorList* err = errlist_create();
    const char* src = "void foo(void) {\n"
                      "    return;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL);
    // Should have at least one error since contract is required
    assert(errlist_count(err) > 0 || err->has_errors);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_contract_forms(void) {
    TEST("[[post-only] contract");
    ErrorList* err = errlist_create();
    const char* src = "[[result == 0]\n"
                      "int32_t zero(void) {\n"
                      "    return 0;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("[pre]] contract (no post)");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[x != 0]]\n"
                       "int32_t invert(int32_t x) {\n"
                       "    return -x;\n"
                       "}\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    assert(root2->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();
}

static void test_when_guard(void) {
    TEST("parse when guard (arrow form)");
    ErrorList* err = errlist_create();
    const char* src = "[x != 0][1]\n"
                      "int32_t div10(int32_t x) {\n"
                      "    when x > 0 -> return x / 10;\n"
                      "    return x / 10;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_when_block(void) {
    TEST("parse when guard (block form)");
    ErrorList* err = errlist_create();
    const char* src = "[x >= 0][result == x]\n"
                      "int32_t abs(int32_t x) {\n"
                      "    when x < 0 {\n"
                      "        return -x;\n"
                      "    }\n"
                      "    return x;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_when_seq(void) {
    TEST("parse sequential when guards");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t sign(int32_t x) {\n"
                      "    when x > 0 -> return 1;\n"
                      "    when x < 0 -> return -1;\n"
                      "    return 0;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_if_blocked(void) {
    TEST("if keyword produces error");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t test(int32_t x) {\n"
                      "    if (x > 0) { return 1; }\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    // Should have errors about if not being a C² construct
    assert(errlist_count(err) > 0 || err->has_errors);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_goto_blocked(void) {
    TEST("goto keyword produces error");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "void test(void) {\n"
                      "    goto cleanup;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(errlist_count(err) > 0 || err->has_errors);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_while_statement(void) {
    TEST("parse while loop");
    ErrorList* err = errlist_create();
    const char* src = "[n >= 0][result == n * (n + 1) / 2]\n"
                      "int32_t sum_n(int32_t n) {\n"
                      "    int32_t i = 0;\n"
                      "    int32_t s = 0;\n"
                      "    while (i <= n) {\n"
                      "        s = s + i;\n"
                      "        i = i + 1;\n"
                      "    }\n"
                      "    return s;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_for_statement(void) {
    TEST("parse for loop");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "void count(void) {\n"
                      "    for (int32_t i = 0; i < 10; i = i + 1) {\n"
                      "        ;\n"
                      "    }\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_derivation_block(void) {
    TEST("parse derivation block");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t add(int32_t a, int32_t b) := {\n"
                      "    1, 2 -> 3;\n"
                      "    10, 20 -> 30;\n"
                      "};\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_derivation_tolerance(void) {
    TEST("parse derivation with tolerance");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "float f(float x) := {\n"
                      "    1.0 -> [0.01] 2.0;\n"
                      "    0.0 -> [0.1] 0.5;\n"
                      "};\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    AstNode* func = root->children[0];
    assert(func->kind == NODE_FUNCTION);
    // Find derivation block
    AstNode* deriv = NULL;
    for (size_t i = 0; i < func->child_count; i++) {
        if (func->children[i]->kind == NODE_DERIVATION) { deriv = func->children[i]; break; }
    }
    assert(deriv != NULL);
    assert(deriv->child_count == 2);
    // Each example should have: [input, output, tolerance_node]
    for (size_t i = 0; i < deriv->child_count; i++) {
        AstNode* ex = deriv->children[i];
        assert(ex->kind == NODE_DERIV_EXAMPLE);
        assert(ex->child_count == 3);
        assert(ex->children[0]->kind == NODE_LITERAL_FLOAT);  // input
        assert(ex->children[1]->kind == NODE_LITERAL_FLOAT);  // output
        assert(ex->children[2]->kind == NODE_DERIV_TOLERANCE); // tolerance wrapper
        assert(ex->children[2]->child_count == 1);
        assert(ex->children[2]->children[0]->kind == NODE_LITERAL_FLOAT); // tolerance value
    }
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_no_derive(void) {
    TEST("parse no_derive function");
    ErrorList* err = errlist_create();
    const char* src = "no_derive [1][1]\n"
                      "int32_t stub(int32_t x);\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_extern_function(void) {
    TEST("parse extern function declaration");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "extern int32_t printf(int32_t x);\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_EXTERN);
    assert(root->children[0]->child_count == 1);
    assert(root->children[0]->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_extern_variable(void) {
    TEST("parse extern variable declaration");
    ErrorList* err = errlist_create();
    const char* src = "extern int32_t errno;\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_EXTERN);
    assert(root->children[0]->child_count == 1);
    assert(root->children[0]->children[0]->kind == NODE_DECL);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_extern_func_with_body(void) {
    TEST("parse extern with body (ignores extern)");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "extern int32_t foo() { return 0; }\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    // extern with body should be treated as a regular function
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_static_function(void) {
    TEST("parse static function");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "static int32_t helper(int32_t x) { return x; }\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    assert(root->children[0]->children[0]->kind == NODE_VARIABLE);
    // Return type node should have NODE_FLAG_STATIC set
    assert(root->children[0]->children[0]->flags & NODE_FLAG_STATIC);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_static_var(void) {
    TEST("parse static variable");
    ErrorList* err = errlist_create();
    const char* src = "static int32_t counter = 0;\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    // Global variable decls are wrapped in NODE_EXPR_STMT
    assert(root->children[0]->kind == NODE_EXPR_STMT);
    assert(root->children[0]->child_count == 1);
    AstNode* decl = root->children[0]->children[0];
    assert(decl->kind == NODE_DECL);
    assert(decl->children[0]->kind == NODE_VARIABLE);
    // Type node should have NODE_FLAG_STATIC set
    assert(decl->children[0]->flags & NODE_FLAG_STATIC);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_const_param(void) {
    TEST("parse const parameter");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t get_value(const int32_t x) { return x; }\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    // Find the parameter NODE_DECL
    AstNode* params = NULL;
    for (size_t i = 0; i < root->children[0]->child_count; i++) {
        if (root->children[0]->children[i]->kind == NODE_PARAM_LIST) {
            params = root->children[0]->children[i];
            break;
        }
    }
    assert(params != NULL && params->child_count == 1);
    AstNode* param_decl = params->children[0];
    assert(param_decl->kind == NODE_DECL);
    assert(param_decl->children[0]->flags & NODE_FLAG_CONST);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_expressions(void) {
    TEST("parse complex expression");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t compute(int32_t a, int32_t b) {\n"
                      "    return (a + b) * (a - b) / 2;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_FUNCTION);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse ternary expression");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\n"
                       "int32_t max(int32_t a, int32_t b) {\n"
                       "    return a > b ? a : b;\n"
                       "}\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();
}

static void test_pointer_type(void) {
    TEST("parse pointer parameter");
    ErrorList* err = errlist_create();
    const char* src = "[ptr != 0][1]\n"
                      "void write(int32_t* ptr, int32_t val) {\n"
                      "    *ptr = val;\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_struct_decl(void) {
    TEST("parse struct with fields");
    ErrorList* err = errlist_create();
    const char* src = "struct Point {\n"
                      "    int32_t x;\n"
                      "    int32_t y;\n"
                      "};\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_STRUCT_DECL);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse struct with name and fields");
    ErrorList* err2 = errlist_create();
    const char* src2 = "struct Point {\n"
                       "    int32_t x;\n"
                       "    int32_t y;\n"
                       "};\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    assert(root2->children[0]->kind == NODE_STRUCT_DECL);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();

    TEST("parse struct with variable");
    ErrorList* err3 = errlist_create();
    const char* src3 = "struct { int32_t x; int32_t y; } point;\n";
    AstNode* root3 = parse_source(src3, err3);
    assert(root3 != NULL && root3->child_count == 1);
    assert(root3->children[0]->kind == NODE_EXPR_STMT);
    ast_free_tree(root3);
    errlist_destroy(err3);
    PASS();
}

static void test_union_decl(void) {
    TEST("parse union with fields");
    ErrorList* err = errlist_create();
    const char* src = "union Data {\n"
                      "    int32_t i;\n"
                      "    float f;\n"
                      "};\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_UNION_DECL);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();
}

static void test_enum_decl(void) {
    TEST("parse enum without values");
    ErrorList* err = errlist_create();
    const char* src = "enum Color {\n"
                      "    RED,\n"
                      "    GREEN,\n"
                      "    BLUE\n"
                      "};\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_ENUM_DECL);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse enum with values");
    ErrorList* err2 = errlist_create();
    const char* src2 = "enum Status {\n"
                       "    OK = 0,\n"
                       "    ERROR = -1\n"
                       "};\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    assert(root2->children[0]->kind == NODE_ENUM_DECL);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();
}

static void test_typedef(void) {
    TEST("parse typedef");
    ErrorList* err = errlist_create();
    const char* src = "typedef int32_t myint;\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_TYPEDEF);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse struct typedef");
    ErrorList* err2 = errlist_create();
    const char* src2 = "typedef struct Point { int32_t x; int32_t y; } Point;\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    assert(root2->children[0]->kind == NODE_TYPEDEF);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();
}

static void test_switch_basic(void) {
    TEST("parse switch with case and default");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "int32_t f(int32_t x) {\n"
                      "    switch (x) {\n"
                      "    case 0:\n"
                      "        return 10;\n"
                      "    case 1:\n"
                      "        return 20;\n"
                      "    default:\n"
                      "        return -1;\n"
                      "    }\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    AstNode* func = root->children[0];
    assert(func->kind == NODE_FUNCTION);
    // Function: [0]=ret_type, [1]=pre, [2]=post, [3]=params, [4]=body
    AstNode* body = func->children[4];
    assert(body->kind == NODE_BLOCK);
    assert(body->child_count == 1);
    assert(body->children[0]->kind == NODE_SWITCH);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse switch with empty body");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\n"
                       "void f(void) {\n"
                       "    switch (0) {}\n"
                       "}\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();

    TEST("parse switch with fall-through");
    ErrorList* err3 = errlist_create();
    const char* src3 = "[1][1]\n"
                       "void f(int32_t x) {\n"
                       "    switch (x) {\n"
                       "    case 0:\n"
                       "        foo();\n"
                       "    case 1:\n"
                       "        bar();\n"
                       "        break;\n"
                       "    }\n"
                       "}\n";
    AstNode* root3 = parse_source(src3, err3);
    assert(root3 != NULL);
    ast_free_tree(root3);
    errlist_destroy(err3);
    PASS();
}

static void test_do_while(void) {
    TEST("parse do-while with simple body");
    ErrorList* err = errlist_create();
    const char* src = "[1][1]\n"
                      "void f(int32_t x) {\n"
                      "    do\n"
                      "        x = x + 1;\n"
                      "    while (x < 10);\n"
                      "}\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    AstNode* func = root->children[0];
    assert(func->kind == NODE_FUNCTION);
    AstNode* body = func->children[4];
    assert(body->kind == NODE_BLOCK);
    assert(body->child_count == 1);
    assert(body->children[0]->kind == NODE_DO_WHILE);
    assert(body->children[0]->child_count == 2);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse do-while with compound body");
    ErrorList* err2 = errlist_create();
    const char* src2 = "[1][1]\n"
                       "void f(int32_t x) {\n"
                       "    do {\n"
                       "        x = x + 1;\n"
                       "    } while (x < 10);\n"
                       "}\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    AstNode* func2 = root2->children[0];
    assert(func2->kind == NODE_FUNCTION);
    AstNode* body2 = func2->children[4];
    assert(body2->kind == NODE_BLOCK);
    assert(body2->child_count == 1);
    assert(body2->children[0]->kind == NODE_DO_WHILE);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();
}

static void test_global_var(void) {
    TEST("parse global variable");
    ErrorList* err = errlist_create();
    const char* src = "int32_t counter = 0;\n";
    AstNode* root = parse_source(src, err);
    assert(root != NULL && root->child_count == 1);
    assert(root->children[0]->kind == NODE_EXPR_STMT);
    ast_free_tree(root);
    errlist_destroy(err);
    PASS();

    TEST("parse global array");
    ErrorList* err2 = errlist_create();
    const char* src2 = "int32_t buffer[256];\n";
    AstNode* root2 = parse_source(src2, err2);
    assert(root2 != NULL && root2->child_count == 1);
    assert(root2->children[0]->kind == NODE_EXPR_STMT);
    ast_free_tree(root2);
    errlist_destroy(err2);
    PASS();
}

int main(void) {
    printf("\nC² Parser Unit Tests\n");
    printf("====================\n\n");

    test_empty_file();
    test_simple_function();
    test_func_no_contract();
    test_contract_forms();
    test_when_guard();
    test_when_block();
    test_when_seq();
    test_if_blocked();
    test_goto_blocked();
    test_while_statement();
    test_for_statement();
    test_derivation_block();
    test_derivation_tolerance();
    test_no_derive();
    test_extern_function();
    test_extern_variable();
    test_extern_func_with_body();
    test_static_function();
    test_static_var();
    test_const_param();
    test_expressions();
    test_pointer_type();
    test_struct_decl();
    test_union_decl();
    test_enum_decl();
    test_typedef();
    test_switch_basic();
    test_do_while();
    test_global_var();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

#include <iostream>
#include <string>

#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "../src/sema/BodyChecker.h"
#include "../src/sema/Sema.h"
#include "test_framework.h"

using namespace glass;

namespace {

std::vector<std::string> checkSource(const std::string& src) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        std::vector<std::string> errs = {"[НЕ РАЗОБРАЛОСЬ]"};
        for (auto& e : parser.errors()) errs.push_back(e);
        return errs;
    }
    Sema sema;
    auto declErrs = sema.analyze(prog);
    if (!declErrs.empty()) {
        std::vector<std::string> errs = {"[ОШИБКИ ДЕКЛАРАЦИЙ]"};
        for (auto& e : declErrs) errs.push_back(e);
        return errs;
    }
    BodyChecker checker(sema);
    return checker.checkProgram(prog);
}

void expectClean(const std::string& name, const std::string& src) {
    auto errs = checkSource(src);
    if (!errs.empty()) {
        std::cerr << "FAILED: " << name << " ожидал 0 ошибок, получил " << errs.size() << ":\n";
        for (auto& e : errs) std::cerr << "    " << e << "\n";
    }
    CHECK(errs.empty());
}

void expectErrorContaining(const std::string& name, const std::string& src, const std::string& needle) {
    auto errs = checkSource(src);
    bool found = false;
    for (auto& e : errs)
        if (e.find(needle) != std::string::npos) found = true;
    if (!found) {
        std::cerr << "FAILED: " << name << " ожидал ошибку с подстрокой '" << needle << "', получил "
                  << errs.size() << " ошибок:\n";
        for (auto& e : errs) std::cerr << "    " << e << "\n";
    }
    CHECK(found);
}

const char* PREAMBLE = "fn main() { }\n";  // чтобы Sema не жаловалась на отсутствие точки входа

// --- Литералы и базовые операторы ---

void test_numeric_binary_ops_widen_to_double() {
    expectClean("numeric_binary_ops_widen_to_double",
                std::string(PREAMBLE) + "fn f() { let x: double = 1 + 2.5; }");
}

void test_string_concatenation_allowed() {
    expectClean("string_concatenation_allowed",
                std::string(PREAMBLE) + "fn f() { let x: string = \"a\" + \"b\"; }");
}

void test_string_plus_int_is_allowed() {
    // "a" + 1 — склейка с автоматическим преобразованием числа в строку (см. tests/language_tests.cpp)
    expectClean("string_plus_int_is_allowed", std::string(PREAMBLE) + "fn f() { let x = \"a\" + 1; }");
}

void test_comparison_requires_numeric() {
    expectErrorContaining("comparison_requires_numeric",
                           std::string(PREAMBLE) + "fn f() { let x = true < false; }", "числовые операнды");
}

void test_logical_requires_bool() {
    expectErrorContaining("logical_requires_bool", std::string(PREAMBLE) + "fn f() { let x = 1 && 2; }",
                           "требует bool операнды");
}

// --- let ---

void test_let_explicit_type_mismatch_is_error() {
    expectErrorContaining("let_explicit_type_mismatch_is_error",
                           std::string(PREAMBLE) + "fn f() { let x: bool = 5; }", "не совместим с ожидаемым");
}

void test_let_inferred_type_works() {
    expectClean("let_inferred_type_works", std::string(PREAMBLE) + "fn f() { let x = 5; let y = x + 1; }");
}

void test_unknown_identifier_is_error() {
    expectErrorContaining("unknown_identifier_is_error", std::string(PREAMBLE) + "fn f() { let x = y; }",
                           "неизвестный идентификатор 'y'");
}

// --- if/while/for ---

void test_if_condition_must_be_bool() {
    expectErrorContaining("if_condition_must_be_bool", std::string(PREAMBLE) + "fn f() { if 5 { } }",
                           "'if' должно быть bool");
}

void test_while_condition_must_be_bool() {
    expectErrorContaining("while_condition_must_be_bool", std::string(PREAMBLE) + "fn f() { while 5 { } }",
                           "'while' должно быть bool");
}

void test_for_range_binds_int() {
    expectClean("for_range_binds_int",
                std::string(PREAMBLE) + "fn f() { for i in 0..10 { let x: int = i; } }");
}

void test_for_over_array_binds_element_type() {
    expectClean("for_over_array_binds_element_type",
                std::string(PREAMBLE) +
                    "fn f(items: [int]) { for i in items { let x: int = i; } }");
}

void test_break_outside_loop_is_error() {
    expectErrorContaining("break_outside_loop_is_error", std::string(PREAMBLE) + "fn f() { break; }",
                           "'break' вне цикла");
}

void test_continue_inside_loop_is_clean() {
    expectClean("continue_inside_loop_is_clean",
                std::string(PREAMBLE) + "fn f() { while true { continue; } }");
}

// --- return ---

void test_return_type_mismatch_is_error() {
    expectErrorContaining("return_type_mismatch_is_error",
                           std::string(PREAMBLE) + "fn f() -> bool { return 5; }", "не совместим с ожидаемым");
}

void test_return_matching_type_is_clean() {
    expectClean("return_matching_type_is_clean", std::string(PREAMBLE) + "fn f() -> int { return 5; }");
}

// --- вызовы функций/конструкция ---

void test_function_call_arg_count_mismatch() {
    expectErrorContaining("function_call_arg_count_mismatch",
                           std::string(PREAMBLE) + "fn f(x: int) { }\nfn g() { f(1, 2); }",
                           "неверное число аргументов");
}

void test_function_call_arg_type_mismatch() {
    expectErrorContaining("function_call_arg_type_mismatch",
                           std::string(PREAMBLE) + "fn f(x: int) { }\nfn g() { f(true); }",
                           "не совместим с ожидаемым");
}

void test_mold_construction_and_field_read() {
    expectClean("mold_construction_and_field_read",
                std::string(PREAMBLE) +
                    "mold Circle {\n    let radius: double\n    Circle(r: double) { radius = r; }\n}\n"
                    "fn f() { let c = Circle(3.0); let r: double = c.radius; }");
}

void test_method_call_on_mold() {
    expectClean("method_call_on_mold",
                std::string(PREAMBLE) +
                    "mold Circle {\n    fn area() -> double { return 1.0; }\n}\n"
                    "fn f() { let c = Circle(); let a: double = c.area(); }");
}

void test_method_call_on_facet() {
    expectClean("method_call_on_facet",
                std::string(PREAMBLE) +
                    "facet Shape {\n    fn area() -> double;\n}\n"
                    "mold Circle : Shape {\n    fn area() -> double { return 1.0; }\n}\n"
                    "fn f(s: Shape) { let a: double = s.area(); }\nfn g() { f(Circle()); }");
}

void test_print_accepts_anything() {
    expectClean("print_accepts_anything",
                std::string(PREAMBLE) + "fn f() { print(5); print(\"x\"); print(true); }");
}

// --- this / super ---

void test_this_outside_method_is_error() {
    expectErrorContaining("this_outside_method_is_error", std::string(PREAMBLE) + "fn f() { let x = this; }",
                           "'this' используется вне метода");
}

void test_super_call_checks_arity() {
    expectErrorContaining("super_call_checks_arity",
                           std::string(PREAMBLE) +
                               "mold A {\n    A(x: int) { }\n}\n"
                               "mold B : A {\n    B() { super(); }\n}",
                           "super(...)");
}

void test_super_call_correct_arity_is_clean() {
    expectClean("super_call_correct_arity_is_clean",
                std::string(PREAMBLE) +
                    "mold A {\n    A(x: int) { }\n}\n"
                    "mold B : A {\n    B(x: int) { super(x); }\n}");
}

// --- индексация и массивы ---

void test_index_requires_int() {
    expectErrorContaining("index_requires_int",
                           std::string(PREAMBLE) + "fn f(items: [int]) { let x = items[true]; }",
                           "индекс должен быть 'int'");
}

void test_array_literal_heterogeneous_but_facet_compatible() {
    // Circle и Square оба реализуют Shape — массив [Shape] с обоими элементами корректен
    expectClean("array_literal_heterogeneous_but_facet_compatible",
                std::string(PREAMBLE) +
                    "facet Shape {\n    fn area() -> double;\n}\n"
                    "mold Circle : Shape {\n    fn area() -> double { return 1.0; }\n}\n"
                    "mold Square : Shape {\n    fn area() -> double { return 2.0; }\n}\n"
                    "fn f() { let shapes: [Shape] = [Circle(), Square()]; }");
}

// --- cast ---

void test_cast_to_unknown_type_is_error() {
    expectErrorContaining("cast_to_unknown_type_is_error", std::string(PREAMBLE) + "fn f() { let x = (Ghost)5; }",
                           "неизвестный тип приведения");
}

void test_cast_to_known_type_is_clean() {
    expectClean("cast_to_known_type_is_clean", std::string(PREAMBLE) + "fn f() { let x = (double)5; }");
}

// --- Полный пример из grammar.md ---

void test_full_grammar_example_is_clean() {
    std::string src =
        "facet Shape {\n"
        "    fn area() -> double;\n"
        "}\n"
        "\n"
        "mold Circle : Shape {\n"
        "    let radius: double\n"
        "\n"
        "    Circle(r: double) {\n"
        "        radius = r\n"
        "    }\n"
        "\n"
        "    fn area() -> double {\n"
        "        return 3.14159 * radius * radius\n"
        "    }\n"
        "}\n"
        "\n"
        "mold Square : Shape {\n"
        "    let side: double\n"
        "\n"
        "    Square(s: double) {\n"
        "        side = s\n"
        "    }\n"
        "\n"
        "    fn area() -> double {\n"
        "        return side * side\n"
        "    }\n"
        "}\n"
        "\n"
        "fn totalArea(shapes: [Shape]) -> double {\n"
        "    let total: double = 0\n"
        "    for s in shapes {\n"
        "        total += s.area()\n"
        "    }\n"
        "    return total\n"
        "}\n"
        "\n"
        "fn main() {\n"
        "    let shapes: [Shape] = [Circle(3.0), Square(4.0)]\n"
        "\n"
        "    try {\n"
        "        print(totalArea(shapes))\n"
        "        print(shapes[0]~vtable)\n"
        "        print(shapes[0]~address)\n"
        "    } catch e: Exception {\n"
        "        print(\"Ошибка: \" + e.message)\n"
        "    }\n"
        "}\n";
    expectClean("full_grammar_example_is_clean", src);
}

} // namespace

int main() {
    test_numeric_binary_ops_widen_to_double();
    test_string_concatenation_allowed();
    test_string_plus_int_is_allowed();
    test_comparison_requires_numeric();
    test_logical_requires_bool();

    test_let_explicit_type_mismatch_is_error();
    test_let_inferred_type_works();
    test_unknown_identifier_is_error();

    test_if_condition_must_be_bool();
    test_while_condition_must_be_bool();
    test_for_range_binds_int();
    test_for_over_array_binds_element_type();
    test_break_outside_loop_is_error();
    test_continue_inside_loop_is_clean();

    test_return_type_mismatch_is_error();
    test_return_matching_type_is_clean();

    test_function_call_arg_count_mismatch();
    test_function_call_arg_type_mismatch();
    test_mold_construction_and_field_read();
    test_method_call_on_mold();
    test_method_call_on_facet();
    test_print_accepts_anything();

    test_this_outside_method_is_error();
    test_super_call_checks_arity();
    test_super_call_correct_arity_is_clean();

    test_index_requires_int();
    test_array_literal_heterogeneous_but_facet_compatible();

    test_cast_to_unknown_type_is_error();
    test_cast_to_known_type_is_clean();

    test_full_grammar_example_is_clean();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

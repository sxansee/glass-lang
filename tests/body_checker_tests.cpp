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

// --- все пути возвращают значение ---

void test_missing_return_if_without_else_is_error() {
    expectErrorContaining("missing_return_if_without_else_is_error",
                           std::string(PREAMBLE) + "fn f(x: int) -> int { if x > 0 { return 1; } }",
                           "не все ветки функции 'f' возвращают значение типа int");
}

void test_missing_return_error_points_at_the_function() {
    auto errs = checkSource(std::string(PREAMBLE) + "fn f(x: int) -> int { if x > 0 { return 1; } }");
    CHECK_EQ(errs.size(), static_cast<size_t>(1));
    // PREAMBLE занимает строку 1; f объявлена на строке 2, в колонке 1 (ключевое слово fn)
    CHECK(!errs.empty() && errs[0].rfind("2:1: не все ветки", 0) == 0);
}

void test_return_on_every_branch_is_clean() {
    expectClean("return_on_every_branch_is_clean",
                std::string(PREAMBLE) +
                    "fn f(x: int) -> int { if x > 0 { return 1; } else if x < 0 { return -1; } else { return 0; } }");
}

void test_return_after_if_is_clean() {
    expectClean("return_after_if_is_clean",
                std::string(PREAMBLE) + "fn f(x: int) -> int { if x > 0 { return 1; } return 0; }");
}

void test_while_true_without_break_counts_as_exit() {
    expectClean("while_true_without_break_counts_as_exit",
                std::string(PREAMBLE) + "fn f() -> int { while true { return 1; } }");
}

void test_while_true_with_break_is_missing_return() {
    expectErrorContaining("while_true_with_break_is_missing_return",
                           std::string(PREAMBLE) + "fn f(x: int) -> int { while true { if x > 0 { break; } } }",
                           "не все ветки функции 'f'");
}

void test_while_true_break_in_nested_loop_still_counts_as_exit() {
    expectClean("while_true_break_in_nested_loop_still_counts_as_exit",
                std::string(PREAMBLE) +
                    "fn f(xs: [int]) -> int { while true { for x in xs { break; } return 1; } }");
}

void test_while_with_variable_condition_is_missing_return() {
    expectErrorContaining("while_with_variable_condition_is_missing_return",
                           std::string(PREAMBLE) + "fn f(c: bool) -> int { while c { return 1; } }",
                           "не все ветки функции 'f'");
}

void test_for_loop_is_missing_return() {
    expectErrorContaining("for_loop_is_missing_return",
                           std::string(PREAMBLE) + "fn f(xs: [int]) -> int { for x in xs { return x; } }",
                           "не все ветки функции 'f'");
}

void test_throw_counts_as_exit() {
    expectClean("throw_counts_as_exit",
                std::string(PREAMBLE) + "fn f() -> int { throw Exception(\"нет\"); }\n"
                                        "mold Exception { let message: string\n    Exception(m: string) { message = m; } }");
}

void test_try_catch_both_return_is_clean() {
    expectClean("try_catch_both_return_is_clean",
                std::string(PREAMBLE) +
                    "mold Exception { let message: string\n    Exception(m: string) { message = m; } }\n"
                    "fn f() -> int { try { return 1; } catch e: Exception { return 2; } }");
}

void test_try_catch_without_return_in_catch_is_error() {
    expectErrorContaining("try_catch_without_return_in_catch_is_error",
                           std::string(PREAMBLE) +
                               "mold Exception { let message: string\n    Exception(m: string) { message = m; } }\n"
                               "fn f() -> int { try { return 1; } catch e: Exception { print(e.message); } }",
                           "не все ветки функции 'f'");
}

void test_finally_that_throws_counts_as_exit() {
    expectClean("finally_that_throws_counts_as_exit",
                std::string(PREAMBLE) +
                    "mold Exception { let message: string\n    Exception(m: string) { message = m; } }\n"
                    "fn f() -> int { try { print(1); } finally { throw Exception(\"x\"); } }");
}

void test_finally_without_exit_does_not_help() {
    expectErrorContaining("finally_without_exit_does_not_help",
                           std::string(PREAMBLE) + "fn f() -> int { try { print(1); } finally { print(2); } }",
                           "не все ветки функции 'f'");
}

void test_code_after_exit_is_not_a_missing_return() {
    // Недостижимый код после return — не повод жаловаться на отсутствие return.
    expectClean("code_after_exit_is_not_a_missing_return",
                std::string(PREAMBLE) + "fn f() -> int { return 1; print(2); }");
}

void test_void_function_needs_no_return() {
    expectClean("void_function_needs_no_return",
                std::string(PREAMBLE) + "fn f(x: int) { if x > 0 { print(x); } }");
}

void test_method_missing_return_is_error() {
    expectErrorContaining("method_missing_return_is_error",
                           std::string(PREAMBLE) +
                               "mold A {\n    fn f(x: int) -> int { if x > 0 { return 1; } }\n}",
                           "не все ветки метода 'f' mold'а 'A' возвращают значение типа int");
}

void test_method_and_constructor_without_result_are_clean() {
    expectClean("method_and_constructor_without_result_are_clean",
                std::string(PREAMBLE) +
                    "mold A {\n    let n: int\n    A(v: int) { if v > 0 { n = v; } }\n"
                    "    fn show() { if n > 0 { print(n); } }\n}");
}

// --- вызов метода без this. ---

void test_method_call_without_this_is_clean() {
    expectClean("method_call_without_this_is_clean",
                std::string(PREAMBLE) +
                    "mold A {\n    fn f() -> int { return g(2) + g(3); }\n    fn g(n: int) -> int { return n; }\n}");
}

void test_method_call_without_this_in_constructor_is_clean() {
    expectClean("method_call_without_this_in_constructor_is_clean",
                std::string(PREAMBLE) +
                    "mold A {\n    let x: int\n    A() { x = twice(2); }\n    fn twice(n: int) -> int { return n * 2; }\n}");
}

void test_method_call_without_this_checks_arguments() {
    expectErrorContaining("method_call_without_this_checks_arguments",
                           std::string(PREAMBLE) +
                               "mold A {\n    fn f() -> int { return g(1); }\n    fn g(a: int, b: int) -> int { return a; }\n}",
                           "неверное число аргументов");
    expectErrorContaining("method_call_without_this_checks_argument_types",
                           std::string(PREAMBLE) +
                               "mold A {\n    fn f() -> int { return g(true); }\n    fn g(a: int) -> int { return a; }\n}",
                           "не совместим с ожидаемым");
}

void test_inherited_method_call_without_this_is_clean() {
    expectClean("inherited_method_call_without_this_is_clean",
                std::string(PREAMBLE) +
                    "mold A {\n    fn g() -> int { return 1; }\n}\n"
                    "mold B : A {\n    fn f() -> int { return g(); }\n}");
}

void test_private_base_method_without_this_is_access_error() {
    expectErrorContaining("private_base_method_without_this_is_access_error",
                           std::string(PREAMBLE) +
                               "mold A {\n    private fn g() -> int { return 1; }\n}\n"
                               "mold B : A {\n    fn f() -> int { return g(); }\n}",
                           "метод 'g' объявлен private в mold'е 'A'");
}

void test_function_wins_over_method_without_this() {
    // Глобальная g(x) перекрывает метод g(): вызов g(1) — это функция (у метода другая арность).
    expectClean("function_wins_over_method_without_this",
                std::string(PREAMBLE) +
                    "fn g(x: int) -> int { return x; }\n"
                    "mold A {\n    fn g() -> int { return 2; }\n    fn f() -> int { return g(1); }\n}");
}

void test_builtin_wins_over_method_without_this() {
    // Метод len() вызывает встроенную len(items), а не сам себя.
    expectClean("builtin_wins_over_method_without_this",
                std::string(PREAMBLE) +
                    "mold Stack {\n    let items: [int]\n    fn len() -> int { return len(items); }\n}");
}

void test_local_variable_still_hides_method_name() {
    expectErrorContaining("local_variable_still_hides_method_name",
                           std::string(PREAMBLE) +
                               "mold A {\n    fn g() -> int { return 1; }\n    fn f() -> int { let g = 5; return g(); }\n}",
                           "переменная, а не функция");
}

void test_static_method_still_not_callable_without_this() {
    expectErrorContaining("static_method_still_not_callable_without_this",
                           std::string(PREAMBLE) +
                               "mold A {\n    static fn s() -> int { return 1; }\n    fn f() -> int { return s(); }\n}",
                           "неизвестная функция или mold 's'");
}

void test_unknown_name_in_free_function_is_still_error() {
    expectErrorContaining("unknown_name_in_free_function_is_still_error",
                           std::string(PREAMBLE) + "mold A {\n    fn g() -> int { return 1; }\n}\nfn f() { g(); }",
                           "неизвестная функция или mold 'g'");
}

void test_unknown_name_inside_mold_is_still_error() {
    expectErrorContaining("unknown_name_inside_mold_is_still_error",
                           std::string(PREAMBLE) + "mold A {\n    fn f() -> int { return nothing(); }\n}",
                           "неизвестная функция или mold 'nothing'");
}

void test_method_call_without_this_is_rewritten_to_this_call() {
    // Подмена видна в AST: callee стал this.g — именно на него рассчитан компилятор байткода.
    Lexer lexer(std::string(PREAMBLE) + "mold A {\n    fn f() -> int { return g(); }\n    fn g() -> int { return 1; }\n}");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    Sema sema;
    CHECK(sema.analyze(prog).empty());
    BodyChecker checker(sema);
    CHECK(checker.checkProgram(prog).empty());

    const MoldDecl* mold = nullptr;
    for (auto& decl : prog.declarations) {
        if (auto* m = std::get_if<MoldDecl>(&decl)) mold = m;
    }
    CHECK(mold != nullptr);
    bool rewritten = false;
    if (mold) {
        for (auto& member : mold->members) {
            auto* method = std::get_if<MethodDecl>(&member);
            if (!method || method->name != "f") continue;
            auto* ret = dynamic_cast<ReturnStmt*>(method->body->statements.at(0).get());
            auto* call = ret ? dynamic_cast<CallExpr*>(ret->value.get()) : nullptr;
            auto* access = call ? dynamic_cast<FieldAccessExpr*>(call->callee.get()) : nullptr;
            rewritten = access && access->name == "g" && dynamic_cast<ThisExpr*>(access->target.get()) != nullptr;
        }
    }
    CHECK(rewritten);
}

// --- модификаторы доступа ---

// Общая заготовка: счёт с private-полем и методом, protected-полем и методом, открытым полем и методом.
const char* ACCOUNT =
    "mold Account {\n"
    "    private let balance: int\n"
    "    protected let owner: string\n"
    "    let title: string\n"
    "    public let id: int\n"
    "    Account(b: int) { balance = b; owner = \"x\"; title = \"t\"; id = 1; }\n"
    "    private fn secret() -> int { return balance; }\n"
    "    protected fn hook() -> int { return 1; }\n"
    "    fn open() -> int { return this.secret() + this.hook(); }\n"
    "    public fn show() -> int { return 2; }\n"
    "    fn sameAs(other: Account) -> bool { return balance == other.balance; }\n"
    "    fn copyFrom(other: Account) { balance = other.balance; other.balance = balance; other.balance += 1; }\n"
    "}\n";

std::string withAccount(const std::string& rest) { return std::string(PREAMBLE) + ACCOUNT + rest; }

void test_access_baseline_account_is_clean() {
    // Заодно: свой private-метод через this (open → secret), свой private-член через другой экземпляр
    // (sameAs, copyFrom) и protected-метод из своего mold'а — всё это внутри Account разрешено.
    expectClean("access_baseline_account_is_clean", withAccount(""));
}

void test_private_field_read_outside_is_error() {
    expectErrorContaining("private_field_read_outside_is_error",
                           withAccount("fn f() { let a = Account(1); let x = a.balance; }"),
                           "поле 'balance' объявлено private в mold'е 'Account': обращаться к нему можно только из самого 'Account'");
}

void test_private_field_write_outside_is_error() {
    expectErrorContaining("private_field_write_outside_is_error",
                           withAccount("fn f() { let a = Account(1); a.balance = 5; }"),
                           "поле 'balance' объявлено private");
    expectErrorContaining("private_field_compound_write_outside_is_error",
                           withAccount("fn f() { let a = Account(1); a.balance += 5; }"),
                           "поле 'balance' объявлено private");
}

void test_private_field_in_other_mold_is_error() {
    expectErrorContaining("private_field_in_other_mold_is_error",
                           withAccount("mold Thief {\n    fn take(a: Account) -> int { return a.balance; }\n}"),
                           "поле 'balance' объявлено private в mold'е 'Account'");
}

void test_private_field_by_simple_name_in_descendant_is_error() {
    expectErrorContaining("private_field_by_simple_name_in_descendant_is_error",
                           withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                                       "    fn peek() -> int { return balance; }\n}"),
                           "поле 'balance' объявлено private в mold'е 'Account'");
}

void test_private_field_by_this_in_descendant_is_error() {
    expectErrorContaining("private_field_by_this_in_descendant_is_error",
                           withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                                       "    fn peek() -> int { return this.balance; }\n}"),
                           "поле 'balance' объявлено private в mold'е 'Account'");
}

void test_private_field_write_by_simple_name_in_descendant_is_error() {
    expectErrorContaining("private_field_write_by_simple_name_in_descendant_is_error",
                           withAccount("mold Savings : Account {\n    Savings() { super(1); balance = 0; }\n}"),
                           "поле 'balance' объявлено private");
}

void test_private_field_of_grandparent_is_error_in_grandchild() {
    expectErrorContaining("private_field_of_grandparent_is_error_in_grandchild",
                           withAccount("mold Mid : Account { Mid() { super(1); } }\n"
                                       "mold Leaf : Mid {\n    Leaf() { super(); }\n    fn peek() -> int { return balance; }\n}"),
                           "поле 'balance' объявлено private в mold'е 'Account'");
}

void test_protected_field_in_descendant_is_clean() {
    expectClean("protected_field_in_descendant_is_clean",
                withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                            "    fn who() -> string { return owner + this.owner; }\n"
                            "    fn set() { owner = \"y\"; this.owner = \"z\"; }\n}"));
}

void test_protected_field_in_grandchild_is_clean() {
    expectClean("protected_field_in_grandchild_is_clean",
                withAccount("mold Mid : Account { Mid() { super(1); } }\n"
                            "mold Leaf : Mid {\n    Leaf() { super(); }\n    fn who() -> string { return owner; }\n}"));
}

void test_protected_through_base_reference_is_allowed() {
    // Сознательное отличие от C# (там ошибка CS1540): проверяется место обращения, а не тип ссылки.
    // Описано в docs/language/objects.md и TODO.md — изменится правило, изменятся и они.
    expectClean("protected_through_base_reference_is_allowed",
                withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                            "    fn peek(a: Account) -> string { return a.owner; }\n}"));
}

void test_protected_field_outside_is_error() {
    expectErrorContaining("protected_field_outside_is_error",
                           withAccount("fn f() { let a = Account(1); let x = a.owner; }"),
                           "поле 'owner' объявлено protected в mold'е 'Account': доступ только из 'Account' и его потомков");
}

void test_protected_field_in_unrelated_mold_is_error() {
    expectErrorContaining("protected_field_in_unrelated_mold_is_error",
                           withAccount("mold Other {\n    fn look(a: Account) -> string { return a.owner; }\n}"),
                           "поле 'owner' объявлено protected");
}

void test_public_and_unmarked_members_are_open_everywhere() {
    expectClean("public_and_unmarked_members_are_open_everywhere",
                withAccount("fn f() { let a = Account(1); let t = a.title; let i = a.id; let n = a.show(); let o = a.open(); }"));
}

void test_private_method_outside_is_error() {
    expectErrorContaining("private_method_outside_is_error",
                           withAccount("fn f() { let a = Account(1); let x = a.secret(); }"),
                           "метод 'secret' объявлен private в mold'е 'Account': вызывать его можно только из самого 'Account'");
}

void test_private_method_in_descendant_is_error() {
    expectErrorContaining("private_method_in_descendant_is_error",
                           withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                                       "    fn f() -> int { return this.secret(); }\n}"),
                           "метод 'secret' объявлен private в mold'е 'Account'");
}

void test_super_private_method_is_error() {
    expectErrorContaining("super_private_method_is_error",
                           withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                                       "    fn f() -> int { return super.secret(); }\n}"),
                           "метод 'secret' объявлен private в mold'е 'Account'");
}

void test_super_protected_method_is_clean() {
    expectClean("super_protected_method_is_clean",
                withAccount("mold Savings : Account {\n    Savings() { super(1); }\n"
                            "    fn f() -> int { return super.hook() + this.hook(); }\n}"));
}

void test_protected_method_outside_is_error() {
    expectErrorContaining("protected_method_outside_is_error",
                           withAccount("fn f() { let a = Account(1); let x = a.hook(); }"),
                           "метод 'hook' объявлен protected в mold'е 'Account': доступ только из 'Account' и его потомков");
}

void test_access_checked_on_each_link_of_a_chain() {
    expectErrorContaining("access_checked_on_each_link_of_a_chain",
                           std::string(PREAMBLE) +
                               "mold Inner {\n    private let v: int\n    Inner() { v = 1; }\n}\n"
                               "mold Outer {\n    let inner: Inner\n    Outer() { inner = Inner(); }\n}\n"
                               "fn f() { let o = Outer(); let x = o.inner.v; }",
                           "поле 'v' объявлено private в mold'е 'Inner'");
}

void test_private_field_does_not_hide_a_function_in_descendant() {
    // count — private-поле предка; потомок его не видит, поэтому count() — это обычная функция, а не «переменная».
    expectClean("private_field_does_not_hide_a_function_in_descendant",
                std::string(PREAMBLE) +
                    "fn count() -> int { return 7; }\n"
                    "mold A {\n    private let count: int\n    A() { count = 1; }\n}\n"
                    "mold B : A {\n    fn f() -> int { return count(); }\n}");
}

void test_accessible_field_still_hides_a_function() {
    expectErrorContaining("accessible_field_still_hides_a_function",
                           std::string(PREAMBLE) +
                               "fn count() -> int { return 7; }\n"
                               "mold A {\n    let count: int\n    fn f() -> int { return count(); }\n}",
                           "переменная, а не функция");
}

void test_private_constructor_outside_is_error() {
    expectErrorContaining("private_constructor_outside_is_error",
                           std::string(PREAMBLE) +
                               "mold Single {\n    private Single() { }\n    fn again() -> Single { return Single(); }\n}\n"
                               "fn f() { let s = Single(); }",
                           "конструктор mold'а 'Single' объявлен private: создавать объекты можно только из самого 'Single'");
}

void test_private_constructor_inside_own_mold_is_clean() {
    expectClean("private_constructor_inside_own_mold_is_clean",
                std::string(PREAMBLE) +
                    "mold Single {\n    private Single() { }\n    fn again() -> Single { return Single(); }\n}");
}

void test_private_base_constructor_in_super_is_error() {
    expectErrorContaining("private_base_constructor_in_super_is_error",
                           std::string(PREAMBLE) +
                               "mold Base {\n    private Base() { }\n}\n"
                               "mold Leaf : Base {\n    Leaf() { super(); }\n}",
                           "конструктор mold'а 'Base' объявлен private");
}

void test_protected_constructor_for_descendants_only() {
    expectClean("protected_constructor_super_is_clean",
                std::string(PREAMBLE) +
                    "mold Base {\n    protected Base(x: int) { }\n}\n"
                    "mold Leaf : Base {\n    Leaf() { super(1); }\n}");
    expectErrorContaining("protected_constructor_outside_is_error",
                           std::string(PREAMBLE) + "mold Base {\n    protected Base(x: int) { }\n}\nfn f() { let b = Base(1); }",
                           "конструктор mold'а 'Base' объявлен protected: создавать объекты можно только из 'Base' и его потомков");
}

void test_access_error_keeps_member_type_no_cascade() {
    // Недоступное поле всё равно имеет свой тип: одна ошибка доступа, а не ещё и «тип не совместим».
    auto errs = checkSource(withAccount("fn f() { let a = Account(1); let x: int = a.balance; }"));
    CHECK_EQ(errs.size(), static_cast<size_t>(1));
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

    test_missing_return_if_without_else_is_error();
    test_missing_return_error_points_at_the_function();
    test_return_on_every_branch_is_clean();
    test_return_after_if_is_clean();
    test_while_true_without_break_counts_as_exit();
    test_while_true_with_break_is_missing_return();
    test_while_true_break_in_nested_loop_still_counts_as_exit();
    test_while_with_variable_condition_is_missing_return();
    test_for_loop_is_missing_return();
    test_throw_counts_as_exit();
    test_try_catch_both_return_is_clean();
    test_try_catch_without_return_in_catch_is_error();
    test_finally_that_throws_counts_as_exit();
    test_finally_without_exit_does_not_help();
    test_code_after_exit_is_not_a_missing_return();
    test_void_function_needs_no_return();
    test_method_missing_return_is_error();
    test_method_and_constructor_without_result_are_clean();

    test_method_call_without_this_is_clean();
    test_method_call_without_this_in_constructor_is_clean();
    test_method_call_without_this_checks_arguments();
    test_inherited_method_call_without_this_is_clean();
    test_private_base_method_without_this_is_access_error();
    test_function_wins_over_method_without_this();
    test_builtin_wins_over_method_without_this();
    test_local_variable_still_hides_method_name();
    test_static_method_still_not_callable_without_this();
    test_unknown_name_in_free_function_is_still_error();
    test_unknown_name_inside_mold_is_still_error();
    test_method_call_without_this_is_rewritten_to_this_call();

    test_access_baseline_account_is_clean();
    test_private_field_read_outside_is_error();
    test_private_field_write_outside_is_error();
    test_private_field_in_other_mold_is_error();
    test_private_field_by_simple_name_in_descendant_is_error();
    test_private_field_by_this_in_descendant_is_error();
    test_private_field_write_by_simple_name_in_descendant_is_error();
    test_private_field_of_grandparent_is_error_in_grandchild();
    test_protected_field_in_descendant_is_clean();
    test_protected_field_in_grandchild_is_clean();
    test_protected_through_base_reference_is_allowed();
    test_protected_field_outside_is_error();
    test_protected_field_in_unrelated_mold_is_error();
    test_public_and_unmarked_members_are_open_everywhere();
    test_private_method_outside_is_error();
    test_private_method_in_descendant_is_error();
    test_super_private_method_is_error();
    test_super_protected_method_is_clean();
    test_protected_method_outside_is_error();
    test_access_checked_on_each_link_of_a_chain();
    test_private_field_does_not_hide_a_function_in_descendant();
    test_accessible_field_still_hides_a_function();
    test_private_constructor_outside_is_error();
    test_private_constructor_inside_own_mold_is_clean();
    test_private_base_constructor_in_super_is_error();
    test_protected_constructor_for_descendants_only();
    test_access_error_keeps_member_type_no_cascade();

    test_full_grammar_example_is_clean();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

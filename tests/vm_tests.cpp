#include <iostream>
#include <string>

#include "../src/codegen/Compiler.h"
#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "../src/sema/BodyChecker.h"
#include "../src/sema/Sema.h"
#include "../src/vm/VM.h"
#include "test_framework.h"

using namespace glass;

namespace {

// Полный конвейер: лексер -> парсер -> Sema -> BodyChecker -> компилятор -> VM.
// Возвращает напечатанные строки; при любой ошибке на любом этапе кладёт
// её единственным элементом с префиксом "ERROR: ".
std::vector<std::string> runSource(const std::string& src) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) return {"ERROR: parse: " + parser.errors()[0]};

    Sema sema;
    auto declErrs = sema.analyze(prog);
    if (!declErrs.empty()) return {"ERROR: sema: " + declErrs[0]};

    BodyChecker checker(sema);
    auto bodyErrs = checker.checkProgram(prog);
    if (!bodyErrs.empty()) return {"ERROR: types: " + bodyErrs[0]};

    Compiler compiler(sema);
    CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(prog);
    } catch (const std::exception& e) {
        return {std::string("ERROR: codegen: ") + e.what()};
    }

    VM vm(std::move(compiled));
    vm.run();
    if (!vm.errors().empty()) return {"ERROR: runtime: " + vm.errors()[0]};
    return vm.printedLines();
}

void expectOutput(const std::string& name, const std::string& src, const std::vector<std::string>& expected) {
    auto actual = runSource(src);
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\n  ожидалось:";
        for (auto& l : expected) std::cerr << " [" << l << "]";
        std::cerr << "\n  получено: ";
        for (auto& l : actual) std::cerr << " [" << l << "]";
        std::cerr << "\n";
    }
    CHECK(actual == expected);
}

void expectRuntimeErrorContaining(const std::string& name, const std::string& src, const std::string& needle) {
    auto actual = runSource(src);
    bool found = !actual.empty() && actual[0].find(needle) != std::string::npos;
    if (!found) {
        std::cerr << "FAILED: " << name << " ожидал ошибку с '" << needle << "', получил: ";
        for (auto& l : actual) std::cerr << " [" << l << "]";
        std::cerr << "\n";
    }
    CHECK(found);
}

// --- Арифметика ---

void test_int_arithmetic() {
    expectOutput("int_arithmetic", "fn main() { print(2 + 3 * 4); }", {"14"});
}

void test_int_double_widening() {
    expectOutput("int_double_widening", "fn main() { print(1 + 2.5); }", {"3.5"});
}

void test_string_concat() {
    expectOutput("string_concat", "fn main() { print(\"foo\" + \"bar\"); }", {"foobar"});
}

void test_division_by_zero_is_runtime_error() {
    expectRuntimeErrorContaining("division_by_zero_is_runtime_error", "fn main() { print(1 / 0); }",
                                  "деление на ноль");
}

// --- Переменные и присваивание ---

void test_let_and_reassignment() {
    expectOutput("let_and_reassignment", "fn main() { let x = 1; x = x + 1; x = x + 1; print(x); }", {"3"});
}

void test_compound_assignment() {
    expectOutput("compound_assignment", "fn main() { let x = 10; x += 5; x -= 2; print(x); }", {"13"});
}

void test_assignment_is_an_expression() {
    expectOutput("assignment_is_an_expression", "fn main() { let a = 0; let b = 0; a = b = 5; print(a); print(b); }",
                 {"5", "5"});
}

// --- Управление потоком ---

void test_if_else() {
    expectOutput("if_else", "fn main() { if 1 < 2 { print(\"yes\"); } else { print(\"no\"); } }", {"yes"});
}

void test_else_if_chain() {
    std::string src =
        "fn classify(x: int) {\n"
        "    if x < 0 { print(\"neg\"); } else if x == 0 { print(\"zero\"); } else { print(\"pos\"); }\n"
        "}\n"
        "fn main() { classify(-1); classify(0); classify(1); }";
    expectOutput("else_if_chain", src, {"neg", "zero", "pos"});
}

void test_while_loop() {
    expectOutput("while_loop", "fn main() { let i = 0; while i < 3 { print(i); i += 1; } }", {"0", "1", "2"});
}

void test_for_range_loop() {
    expectOutput("for_range_loop", "fn main() { for i in 0..3 { print(i); } }", {"0", "1", "2"});
}

void test_break_exits_loop() {
    expectOutput("break_exits_loop", "fn main() { for i in 0..10 { if i == 3 { break; } print(i); } }",
                 {"0", "1", "2"});
}

void test_continue_skips_iteration() {
    expectOutput("continue_skips_iteration",
                 "fn main() { for i in 0..5 { if i == 2 { continue; } print(i); } }", {"0", "1", "3", "4"});
}

void test_short_circuit_and_skips_right_side() {
    // Если бы правая часть && вычислялась, деление на 0 привело бы к runtime-ошибке.
    expectOutput("short_circuit_and_skips_right_side", "fn main() { if false && (1 / 0 == 0) { } print(\"ok\"); }",
                 {"ok"});
}

void test_short_circuit_or_skips_right_side() {
    expectOutput("short_circuit_or_skips_right_side", "fn main() { if true || (1 / 0 == 0) { } print(\"ok\"); }",
                 {"ok"});
}

// --- Функции и рекурсия ---

void test_function_call_with_return() {
    expectOutput("function_call_with_return", "fn add(a: int, b: int) -> int { return a + b; }\n"
                                                "fn main() { print(add(2, 3)); }",
                 {"5"});
}

void test_recursion_factorial() {
    std::string src =
        "fn factorial(n: int) -> int {\n"
        "    if n <= 1 { return 1; }\n"
        "    return n * factorial(n - 1);\n"
        "}\n"
        "fn main() { print(factorial(5)); }";
    expectOutput("recursion_factorial", src, {"120"});
}

void test_iterative_fibonacci() {
    std::string src =
        "fn fib(n: int) -> int {\n"
        "    let a = 0;\n"
        "    let b = 1;\n"
        "    for i in 0..n {\n"
        "        let temp = a + b;\n"
        "        a = b;\n"
        "        b = temp;\n"
        "    }\n"
        "    return a;\n"
        "}\n"
        "fn main() { print(fib(10)); }";
    expectOutput("iterative_fibonacci", src, {"55"});
}

void test_void_function_no_explicit_return() {
    expectOutput("void_function_no_explicit_return",
                 "fn greet() { print(\"hi\"); }\nfn main() { greet(); print(\"done\"); }", {"hi", "done"});
}

// --- Массивы ---

void test_array_literal_and_print() {
    expectOutput("array_literal_and_print", "fn main() { print([1, 2, 3]); }", {"[1, 2, 3]"});
}

void test_array_index_read() {
    expectOutput("array_index_read", "fn main() { let a = [10, 20, 30]; print(a[0]); print(a[2]); }",
                 {"10", "30"});
}

void test_array_index_write() {
    expectOutput("array_index_write", "fn main() { let a = [1, 2, 3]; a[1] = 99; print(a); }", {"[1, 99, 3]"});
}

void test_array_compound_index_assignment() {
    // arr[i] += x компилируется через Dup2 (не Dup, как обычное поле) —
    // нужны И массив, И индекс ещё раз, чтобы прочитать текущий элемент.
    // Отдельный тест специально на этот путь кодогенерации.
    expectOutput("array_compound_index_assignment", "fn main() { let a = [1, 2, 3]; a[0] += 10; print(a[0]); }",
                 {"11"});
}

void test_array_index_out_of_bounds_is_runtime_error() {
    expectRuntimeErrorContaining("array_index_out_of_bounds", "fn main() { let a = [1, 2, 3]; print(a[5]); }",
                                  "вне границ");
}

void test_array_negative_index_counts_from_the_end() {
    // a[-1] — последний элемент (как в Python); выход за начало — по-прежнему ошибка границ.
    expectRuntimeErrorContaining("array_negative_index", "fn main() { let a = [1, 2, 3]; print(a[-4]); }",
                                  "вне границ");
}

void test_for_in_array_loop() {
    std::string src =
        "fn main() {\n"
        "    let sum = 0;\n"
        "    for x in [1, 2, 3, 4, 5] {\n"
        "        sum += x;\n"
        "    }\n"
        "    print(sum);\n"
        "}";
    expectOutput("for_in_array_loop", src, {"15"});
}

void test_for_in_array_break_continue() {
    std::string src =
        "fn main() {\n"
        "    let sum = 0;\n"
        "    for x in [1, 2, 3, 4, 5, 6] {\n"
        "        if x == 5 { break; }\n"
        "        if x == 2 { continue; }\n"
        "        sum += x;\n"
        "    }\n"
        "    print(sum);\n"
        "}";
    expectOutput("for_in_array_break_continue", src, {"8"});  // 1+3+4 (2 пропущен continue, 5 — break)
}

void test_empty_array_with_declared_type() {
    // Пустой литерал типизируется только из контекста (см. BodyChecker) —
    // сама VM это не видит, но кодогенерации всё равно, N=0 — законный NewArray.
    expectOutput("empty_array_with_declared_type", "fn main() { let a: [int] = []; print(a); }", {"[]"});
}

void test_nested_array_indexing() {
    expectOutput("nested_array_indexing", "fn main() { let m = [[1, 2], [3, 4]]; print(m[1][0]); }", {"3"});
}

void test_array_is_reference_type() {
    // Массив — ссылочный тип (grammar.md, раздел 5): функция получает тот
    // же массив, не копию, и изменение элемента видно вызывающему.
    std::string src =
        "fn bump(xs: [int]) { xs[0] = xs[0] + 1; }\n"
        "fn main() {\n"
        "    let a = [1, 2, 3];\n"
        "    bump(a);\n"
        "    print(a[0]);\n"
        "}";
    expectOutput("array_is_reference_type", src, {"2"});
}

} // namespace

int main() {
    test_int_arithmetic();
    test_int_double_widening();
    test_string_concat();
    test_division_by_zero_is_runtime_error();

    test_let_and_reassignment();
    test_compound_assignment();
    test_assignment_is_an_expression();

    test_if_else();
    test_else_if_chain();
    test_while_loop();
    test_for_range_loop();
    test_break_exits_loop();
    test_continue_skips_iteration();
    test_short_circuit_and_skips_right_side();
    test_short_circuit_or_skips_right_side();

    test_function_call_with_return();
    test_recursion_factorial();
    test_iterative_fibonacci();
    test_void_function_no_explicit_return();

    test_array_literal_and_print();
    test_array_index_read();
    test_array_index_write();
    test_array_compound_index_assignment();
    test_array_index_out_of_bounds_is_runtime_error();
    test_array_negative_index_counts_from_the_end();
    test_for_in_array_loop();
    test_for_in_array_break_continue();
    test_empty_array_with_declared_type();
    test_nested_array_indexing();
    test_array_is_reference_type();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

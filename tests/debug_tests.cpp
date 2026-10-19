#include <iostream>
#include <memory>
#include <string>

#include "../src/codegen/Compiler.h"
#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "../src/prelude/Prelude.h"
#include "../src/sema/BodyChecker.h"
#include "../src/sema/Sema.h"
#include "../src/vm/VM.h"
#include "test_framework.h"

using namespace glass;

namespace {

// В отличие от остальных наборов тестов, здесь нужен САМ живой VM (для
// пошагового выполнения), а не только его конечный результат — поэтому
// unique_ptr, а не запуск до конца внутри хелпера (VM специально не
// копируется и не перемещается — свой собственный жизненный цикл кучи).
std::unique_ptr<VM> compileForDebug(const std::string& src, std::string* errorOut) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        if (errorOut) *errorOut = "parse: " + parser.errors()[0];
        return nullptr;
    }
    prependPrelude(prog);
    Sema sema;
    auto declErrs = sema.analyze(prog);
    if (!declErrs.empty()) {
        if (errorOut) *errorOut = "sema: " + declErrs[0];
        return nullptr;
    }
    BodyChecker checker(sema);
    auto bodyErrs = checker.checkProgram(prog);
    if (!bodyErrs.empty()) {
        if (errorOut) *errorOut = "types: " + bodyErrs[0];
        return nullptr;
    }
    Compiler compiler(sema);
    try {
        CompiledProgram compiled = compiler.compileProgram(prog);
        return std::make_unique<VM>(std::move(compiled));
    } catch (const std::exception& e) {
        if (errorOut) *errorOut = std::string("codegen: ") + e.what();
        return nullptr;
    }
}

// --- Точки останова / runUntilPause ---

void test_run_until_pause_without_breakpoints_finishes() {
    std::string err;
    auto vm = compileForDebug("fn main() { print(1); print(2); }\n", &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    auto status = vm->runUntilPause();
    CHECK(status == VM::RunStatus::Finished);
    CHECK((vm->printedLines() == std::vector<std::string>{"1", "2"}));
}

void test_run_until_pause_stops_at_breakpoint() {
    std::string src =
        "fn main() {\n"
        "    print(1);\n"
        "    print(2);\n"
        "    print(3);\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(3);
    auto status = vm->runUntilPause();
    CHECK(status == VM::RunStatus::PausedAtBreakpoint);
    CHECK(vm->currentLine() == 3);
    CHECK((vm->printedLines() == std::vector<std::string>{"1"}));  // строка 2 ещё не выполнена

    status = vm->runUntilPause();  // "продолжить"
    CHECK(status == VM::RunStatus::Finished);
    CHECK((vm->printedLines() == std::vector<std::string>{"1", "2", "3"}));
}

void test_breakpoint_inside_loop_hits_each_iteration() {
    std::string src =
        "fn main() {\n"
        "    let i = 0;\n"
        "    while i < 3 {\n"
        "        print(i);\n"
        "        i += 1;\n"
        "    }\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(4);  // print(i)
    int hits = 0;
    while (vm->runUntilPause() == VM::RunStatus::PausedAtBreakpoint) {
        ++hits;
        if (hits > 10) break;  // защита от зависания при регрессии
    }
    CHECK(hits == 3);
    CHECK((vm->printedLines() == std::vector<std::string>{"0", "1", "2"}));
}

// --- stepInto / stepOver / stepOut ---

void test_step_into_advances_one_line_and_enters_calls() {
    std::string src =
        "fn helper() -> int {\n"
        "    return 42;\n"
        "}\n"
        "fn main() {\n"
        "    let x = helper();\n"
        "    print(x);\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // на "let x = helper();" (запускает выполнение)
    CHECK(vm->currentLine() == 5);
    vm->stepInto();  // должны оказаться ВНУТРИ helper(), на line 2
    CHECK(vm->currentLine() == 2);
    CHECK(vm->callStack().size() == 2);  // main + helper
}

// print(объект) вызывает toString() через vtable (опкод ObjToString) — в отладчике это обычный фрейм.
const char* kToStringProgram =
    "mold P {\n"                         // 1
    "    fn toString() -> string {\n"    // 2
    "        return \"p\"\n"             // 3
    "    }\n"                            // 4
    "}\n"                                // 5
    "fn main() {\n"                      // 6
    "    let p = P()\n"                  // 7
    "    print(p)\n"                     // 8
    "    print(\"конец\")\n"             // 9
    "}\n";                               // 10

void test_step_into_enters_tostring_from_print() {
    std::string err;
    auto vm = compileForDebug(kToStringProgram, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // line 7
    CHECK(vm->currentLine() == 7);
    vm->stepOver();  // line 8: print(p) (мимо неявного конструктора P(), в который stepInto зашёл бы)
    CHECK(vm->currentLine() == 8);
    vm->stepInto();  // внутрь toString()
    CHECK(vm->currentLine() == 3);
    CHECK(vm->callStack().size() == 2);  // main + toString
}

void test_step_over_print_runs_tostring_without_stopping_inside() {
    std::string err;
    auto vm = compileForDebug(kToStringProgram, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // line 7
    vm->stepOver();  // line 8 (мимо неявного конструктора P())
    auto status = vm->stepOver();  // print(p) вместе с toString() — одним шагом
    CHECK(status == VM::RunStatus::PausedAfterStep);
    CHECK(vm->currentLine() == 9);
    CHECK(vm->callStack().size() == 1);
    vm->run();
    CHECK(vm->printedLines().size() == 2);
    if (vm->printedLines().size() == 2) CHECK(vm->printedLines()[0] == "p");
}

void test_step_over_does_not_stop_inside_called_function() {
    std::string src =
        "fn helper() -> int {\n"
        "    return 42;\n"
        "}\n"
        "fn main() {\n"
        "    let x = helper();\n"
        "    print(x);\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // line 5
    CHECK(vm->currentLine() == 5);
    auto status = vm->stepOver();  // должен перепрыгнуть helper() целиком
    CHECK(status == VM::RunStatus::PausedAfterStep);
    CHECK(vm->currentLine() == 6);
    CHECK(vm->callStack().size() == 1);  // снова только main — не застряли внутри helper()
}

void test_step_out_returns_to_caller() {
    std::string src =
        "fn helper() -> int {\n"
        "    let y = 1;\n"
        "    return y;\n"
        "}\n"
        "fn main() {\n"
        "    let x = helper();\n"
        "    print(x);\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // line 6
    vm->stepInto();  // внутрь helper(), line 2
    CHECK(vm->callStack().size() == 2);
    auto status = vm->stepOut();
    CHECK(status == VM::RunStatus::PausedAfterStep);
    CHECK(vm->callStack().size() == 1);  // снова в main
}

void test_run_after_stepping_finishes_from_current_point_not_restarts() {
    // run() и step*()/runUntilPause() — разные входы в одну и ту же VM
    // (см. VM.h). Если после начатой пошаговой отладки вызвать run(), он
    // обязан доработать С ТЕКУЩЕГО места, а не воткнуть второй main()
    // поверх уже существующего фрейма.
    std::string src = "fn main() { print(1); print(2); print(3); }\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // встали на первую строку, ничего не выполнив
    CHECK(vm->printedLines().empty());
    int exitCode = vm->run();
    CHECK(exitCode == 0);
    CHECK((vm->printedLines() == std::vector<std::string>{"1", "2", "3"}));
}

// --- Инспекция: locals / callStack ---

void test_locals_reports_current_frame_variables() {
    std::string src =
        "fn main() {\n"
        "    let a = 10;\n"
        "    let b = 20;\n"
        "    print(a + b);\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(4);
    vm->runUntilPause();
    auto locals = vm->locals();
    bool foundA = false, foundB = false;
    for (auto& l : locals) {
        if (l.name == "a" && isInt(l.value) && std::get<long long>(l.value) == 10) foundA = true;
        if (l.name == "b" && isInt(l.value) && std::get<long long>(l.value) == 20) foundB = true;
    }
    CHECK(foundA);
    CHECK(foundB);
}

void test_call_stack_shows_nested_function_names() {
    std::string src =
        "fn inner() -> int {\n"
        "    return 1;\n"
        "}\n"
        "fn outer() -> int {\n"
        "    return inner();\n"
        "}\n"
        "fn main() {\n"
        "    print(outer());\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(2);  // внутри inner()
    auto status = vm->runUntilPause();
    CHECK(status == VM::RunStatus::PausedAtBreakpoint);
    auto stack = vm->callStack();
    // От самого глубокого к main: inner, outer, main
    CHECK(stack.size() == 3);
    if (stack.size() == 3) {
        CHECK(stack[0].functionName == "inner");
        CHECK(stack[1].functionName == "outer");
        CHECK(stack[2].functionName == "main");
    }
}

// --- heapObjects ---

void test_heap_objects_reports_live_object_fields() {
    std::string src =
        "mold Point {\n"
        "    let x: int\n"
        "    let y: int\n"
        "    Point(a: int, b: int) { x = a; y = b; }\n"
        "}\n"
        "fn main() {\n"
        "    let p = Point(3, 4);\n"
        "    print(p.x);\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(8);
    vm->runUntilPause();
    auto heap = vm->heapObjects();
    bool found = false;
    for (auto& obj : heap) {
        if (obj.isArray || obj.className != "Point") continue;
        long long x = -1, y = -1;
        for (auto& f : obj.fields) {
            if (f.name == "x" && isInt(f.value)) x = std::get<long long>(f.value);
            if (f.name == "y" && isInt(f.value)) y = std::get<long long>(f.value);
        }
        if (x == 3 && y == 4) found = true;
    }
    CHECK(found);
}

} // namespace

int main() {
    test_run_until_pause_without_breakpoints_finishes();
    test_run_until_pause_stops_at_breakpoint();
    test_breakpoint_inside_loop_hits_each_iteration();

    test_step_into_advances_one_line_and_enters_calls();
    test_step_over_does_not_stop_inside_called_function();
    test_step_into_enters_tostring_from_print();
    test_step_over_print_runs_tostring_without_stopping_inside();
    test_step_out_returns_to_caller();
    test_run_after_stepping_finishes_from_current_point_not_restarts();

    test_locals_reports_current_frame_variables();
    test_call_stack_shows_nested_function_names();

    test_heap_objects_reports_live_object_fields();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

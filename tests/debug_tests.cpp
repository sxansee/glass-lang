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

void test_step_into_construction_does_not_enter_implicit_initialization() {
    // Mold без инициализаторов полей: в служебную $fieldinit stepInto не заходит — это не код студента.
    std::string src =
        "mold P {\n"                    // 1
        "    let x: int;\n"             // 2
        "}\n"                           // 3
        "fn main() {\n"                 // 4
        "    let p = P();\n"            // 5
        "    print(1);\n"               // 6
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(5);
    vm->runUntilPause();
    CHECK_EQ(vm->currentLine(), 5);
    vm->stepInto();
    CHECK_EQ(vm->currentLine(), 6);                    // сразу следующая строка main
    CHECK_EQ(vm->callStack().size(), static_cast<size_t>(1));
}

void test_step_into_construction_of_derived_mold_without_initializers_steps_over() {
    // Цепочка предков тоже не «заходится»: вызов инициализации предка — служебный код.
    std::string src =
        "mold A {\n"                    // 1
        "    let a: int;\n"             // 2
        "}\n"                           // 3
        "mold B : A {\n"                // 4
        "    let b: int;\n"             // 5
        "}\n"                           // 6
        "fn main() {\n"                 // 7
        "    let v = B();\n"            // 8
        "    print(1);\n"               // 9
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(8);
    vm->runUntilPause();
    vm->stepInto();
    CHECK_EQ(vm->currentLine(), 9);
    CHECK_EQ(vm->callStack().size(), static_cast<size_t>(1));
}

void test_step_into_construction_enters_field_initializer() {
    // А инициализатор поля — код студента: в него заходить можно (и в вызываемую им функцию тоже).
    std::string src =
        "fn five() -> int {\n"          // 1
        "    return 5;\n"               // 2
        "}\n"                           // 3
        "mold P {\n"                    // 4
        "    let x: int = five();\n"    // 5
        "}\n"                           // 6
        "fn main() {\n"                 // 7
        "    let p = P();\n"            // 8
        "    print(1);\n"               // 9
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(8);
    vm->runUntilPause();
    CHECK_EQ(vm->currentLine(), 8);
    vm->stepInto();
    CHECK_EQ(vm->currentLine(), 5);                    // строка инициализатора поля
    CHECK_EQ(vm->callStack().size(), static_cast<size_t>(2));
    vm->stepInto();
    CHECK_EQ(vm->currentLine(), 2);                    // внутрь five()
    CHECK_EQ(vm->callStack().size(), static_cast<size_t>(3));
}

void test_step_into_enters_tostring_from_print() {
    std::string err;
    auto vm = compileForDebug(kToStringProgram, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->stepInto();  // line 7
    CHECK(vm->currentLine() == 7);
    vm->stepOver();  // line 8: print(p)
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
    vm->stepOver();  // line 8
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

// --- static-поля ---

void test_static_vars_report_values_in_declaration_order() {
    std::string src =
        "mold A {\n"                          // 1
        "    static let n: int = 5;\n"        // 2
        "    static let d: double;\n"         // 3
        "    static let flag: bool;\n"        // 4
        "}\n"                                 // 5
        "mold B : A {\n"                      // 6
        "    static let s: string = \"x\";\n" // 7
        "}\n"                                 // 8
        "fn main() {\n"                       // 9
        "    A.n = 6;\n"                      // 10
        "    print(A.n);\n"                   // 11
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(11);
    vm->runUntilPause();
    auto vars = vm->staticVars();
    CHECK_EQ(vars.size(), static_cast<size_t>(4));
    if (vars.size() == 4) {
        CHECK_EQ(vars[0].className + "." + vars[0].name, std::string("A.n"));
        CHECK_EQ(vars[1].className + "." + vars[1].name, std::string("A.d"));
        CHECK_EQ(vars[2].className + "." + vars[2].name, std::string("A.flag"));
        CHECK_EQ(vars[3].className + "." + vars[3].name, std::string("B.s"));
        CHECK(isInt(vars[0].value) && std::get<long long>(vars[0].value) == 6);   // изменено в main
        CHECK(isDouble(vars[1].value) && std::get<double>(vars[1].value) == 0.0);  // значение по умолчанию
        CHECK(isBool(vars[2].value) && !std::get<bool>(vars[2].value));
    }
}

void test_object_held_only_by_static_field_is_reachable() {
    std::string src =
        "mold Box {\n"                              // 1
        "    let v: int\n"                          // 2
        "    Box(x: int) { v = x; }\n"              // 3
        "}\n"                                       // 4
        "mold Registry {\n"                         // 5
        "    static let item: Box = Box(7);\n"      // 6
        "}\n"                                       // 7
        "fn main() {\n"                             // 8
        "    print(1);\n"                           // 9
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(9);
    vm->runUntilPause();
    bool found = false;
    for (auto& obj : vm->heapObjects()) {
        if (obj.isArray || obj.className != "Box") continue;
        found = obj.reachable;   // единственная ссылка на Box — из static-поля, и это корень
    }
    CHECK(found);
}

void test_breakpoint_inside_static_initializer_and_call_stack() {
    std::string src =
        "fn seed() -> int {\n"                      // 1
        "    return 5;\n"                           // 2  <- точка останова
        "}\n"                                       // 3
        "mold A {\n"                                // 4
        "    static let n: int = seed();\n"         // 5
        "}\n"                                       // 6
        "fn main() {\n"                             // 7
        "    print(A.n);\n"                         // 8
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->setBreakpoint(2);
    auto status = vm->runUntilPause();
    CHECK(status == VM::RunStatus::PausedAtBreakpoint);
    CHECK_EQ(vm->currentLine(), 2);
    auto stack = vm->callStack();
    // seed вызвана из инициализатора static-полей, а тот выполняется «поверх» main.
    CHECK_EQ(stack.size(), static_cast<size_t>(3));
    if (stack.size() == 3) {
        CHECK_EQ(stack[0].functionName, std::string("seed"));
        CHECK_EQ(stack[2].functionName, std::string("main"));
    }
    vm->clearAllBreakpoints();
    CHECK(vm->runUntilPause() == VM::RunStatus::Finished);
    CHECK(vm->errors().empty());
    CHECK_EQ(vm->printedLines().size(), static_cast<size_t>(1));
    if (vm->printedLines().size() == 1) CHECK_EQ(vm->printedLines()[0], std::string("5"));
}

void test_static_initializer_error_happens_before_main() {
    std::string src =
        "fn boom() -> int { throw Exception(\"не вышло\"); }\n"
        "mold A {\n"
        "    static let n: int = boom();\n"
        "}\n"
        "fn main() {\n"
        "    print(\"main не должен выполниться\");\n"
        "}\n";
    std::string err;
    auto vm = compileForDebug(src, &err);
    CHECK(vm != nullptr);
    if (!vm) return;
    vm->run();
    CHECK(vm->printedLines().empty());   // main не успел напечатать ни строки
    CHECK_EQ(vm->errors().size(), static_cast<size_t>(1));
    if (vm->errors().size() == 1) {
        CHECK(vm->errors()[0].find("необработанное исключение (Exception): не вышло") != std::string::npos);
    }
}

} // namespace

int main() {
    test_run_until_pause_without_breakpoints_finishes();
    test_run_until_pause_stops_at_breakpoint();
    test_breakpoint_inside_loop_hits_each_iteration();

    test_step_into_advances_one_line_and_enters_calls();
    test_step_over_does_not_stop_inside_called_function();
    test_step_into_construction_does_not_enter_implicit_initialization();
    test_step_into_construction_of_derived_mold_without_initializers_steps_over();
    test_step_into_construction_enters_field_initializer();
    test_step_into_enters_tostring_from_print();
    test_step_over_print_runs_tostring_without_stopping_inside();
    test_step_out_returns_to_caller();
    test_run_after_stepping_finishes_from_current_point_not_restarts();

    test_locals_reports_current_frame_variables();
    test_call_stack_shows_nested_function_names();

    test_heap_objects_reports_live_object_fields();

    test_static_vars_report_values_in_declaration_order();
    test_object_held_only_by_static_field_is_reachable();
    test_breakpoint_inside_static_initializer_and_call_stack();
    test_static_initializer_error_happens_before_main();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

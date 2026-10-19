#include <iostream>
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

struct RunResult {
    std::vector<std::string> output;
    size_t liveObjectsAfterRun = 0;
    bool ok = true;
};

// Тот же пайплайн, что в main.cpp/Repl.cpp — включая пролог с Exception,
// без него catch по Exception был бы недостижим (см. prelude/Prelude.h).
RunResult runSource(const std::string& src) {
    RunResult result;
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        result.ok = false;
        result.output = {"ERROR: parse: " + parser.errors()[0]};
        return result;
    }

    prependPrelude(prog);

    Sema sema;
    auto declErrs = sema.analyze(prog);
    if (!declErrs.empty()) {
        result.ok = false;
        result.output = {"ERROR: sema: " + declErrs[0]};
        return result;
    }

    BodyChecker checker(sema);
    auto bodyErrs = checker.checkProgram(prog);
    if (!bodyErrs.empty()) {
        result.ok = false;
        result.output = {"ERROR: types: " + bodyErrs[0]};
        return result;
    }

    Compiler compiler(sema);
    CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(prog);
    } catch (const std::exception& e) {
        result.ok = false;
        result.output = {std::string("ERROR: codegen: ") + e.what()};
        return result;
    }

    VM vm(std::move(compiled));
    vm.run();
    if (!vm.errors().empty()) {
        result.ok = false;
        result.output = {"ERROR: runtime: " + vm.errors()[0]};
        return result;
    }
    result.output = vm.printedLines();
    result.liveObjectsAfterRun = vm.liveObjectCount();
    return result;
}

void expectOutput(const std::string& name, const std::string& src, const std::vector<std::string>& expected) {
    auto result = runSource(src);
    if (result.output != expected) {
        std::cerr << "FAILED: " << name << " ожидал [";
        for (auto& l : expected) std::cerr << " [" << l << "]";
        std::cerr << " ], получил [";
        for (auto& l : result.output) std::cerr << " [" << l << "]";
        std::cerr << " ]\n";
    }
    CHECK(result.output == expected);
}

void expectContaining(const std::string& name, const std::string& src, const std::string& needle) {
    auto result = runSource(src);
    bool found = !result.output.empty() && result.output[0].find(needle) != std::string::npos;
    if (!found) {
        std::cerr << "FAILED: " << name << " ожидал подстроку '" << needle << "', получил: ";
        for (auto& l : result.output) std::cerr << " [" << l << "]";
        std::cerr << "\n";
    }
    CHECK(found);
}

// --- Базовый throw/catch ---

void test_basic_throw_and_catch() {
    std::string src =
        "fn main() {\n"
        "    try {\n"
        "        throw Exception(\"boom\");\n"
        "    } catch e: Exception {\n"
        "        print(e.message);\n"
        "    }\n"
        "}\n";
    expectOutput("basic_throw_and_catch", src, {"boom"});
}

void test_try_without_throw_runs_normally() {
    std::string src =
        "fn main() {\n"
        "    try {\n"
        "        print(1);\n"
        "    } catch e: Exception {\n"
        "        print(2);\n"
        "    }\n"
        "    print(3);\n"
        "}\n";
    expectOutput("try_without_throw_runs_normally", src, {"1", "3"});
}

void test_uncaught_exception_is_clean_runtime_error() {
    expectContaining("uncaught_exception_is_clean_runtime_error",
                      "fn main() { throw Exception(\"nobody catches me\"); }", "необработанное исключение");
}

void test_exception_message_preserved_through_field() {
    std::string src =
        "fn main() {\n"
        "    let e = Exception(\"hello\");\n"
        "    print(e.message);\n"
        "}\n";
    expectOutput("exception_message_preserved_through_field", src, {"hello"});
}

// --- Наследование: Exception и потомки ---

void test_user_defined_exception_subtype() {
    std::string src =
        "mold NotFoundError : Exception {\n"
        "    let code: int\n"
        "    NotFoundError(msg: string, c: int) {\n"
        "        super(msg);\n"
        "        code = c;\n"
        "    }\n"
        "}\n"
        "fn main() {\n"
        "    try {\n"
        "        throw NotFoundError(\"missing\", 404);\n"
        "    } catch e: NotFoundError {\n"
        "        print(e.message);\n"
        "        print(e.code);\n"
        "    }\n"
        "}\n";
    expectOutput("user_defined_exception_subtype", src, {"missing", "404"});
}

void test_catch_by_base_type_catches_subtype() {
    std::string src =
        "mold NotFoundError : Exception {\n"
        "    NotFoundError(msg: string) { super(msg); }\n"
        "}\n"
        "fn main() {\n"
        "    try {\n"
        "        throw NotFoundError(\"missing\");\n"
        "    } catch e: Exception {\n"
        "        print(\"caught as base: \" + e.message);\n"
        "    }\n"
        "}\n";
    expectOutput("catch_by_base_type_catches_subtype", src, {"caught as base: missing"});
}

void test_multiple_catches_pick_most_specific_first_match() {
    // Порядок catch в исходнике решает, не "специфичность" — первый
    // подходящий побеждает (как в C#/Java). Здесь конкретный тип стоит
    // первым и должен сработать, а не общий Exception следом за ним.
    std::string src =
        "mold NotFoundError : Exception {\n"
        "    NotFoundError(msg: string) { super(msg); }\n"
        "}\n"
        "fn main() {\n"
        "    try {\n"
        "        throw NotFoundError(\"missing\");\n"
        "    } catch e: NotFoundError {\n"
        "        print(\"specific\");\n"
        "    } catch e: Exception {\n"
        "        print(\"generic\");\n"
        "    }\n"
        "}\n";
    expectOutput("multiple_catches_pick_most_specific_first_match", src, {"specific"});
}

void test_non_matching_catch_propagates_to_outer_try() {
    std::string src =
        "mold NotFoundError : Exception {\n"
        "    NotFoundError(msg: string) { super(msg); }\n"
        "}\n"
        "fn main() {\n"
        "    try {\n"
        "        try {\n"
        "            throw Exception(\"plain\");\n"
        "        } catch e: NotFoundError {\n"
        "            print(\"should not happen\");\n"
        "        }\n"
        "    } catch e: Exception {\n"
        "        print(\"outer caught: \" + e.message);\n"
        "    }\n"
        "}\n";
    expectOutput("non_matching_catch_propagates_to_outer_try", src, {"outer caught: plain"});
}

// --- finally ---

void test_finally_runs_on_normal_completion() {
    std::string src =
        "fn main() {\n"
        "    try {\n"
        "        print(1);\n"
        "    } finally {\n"
        "        print(2);\n"
        "    }\n"
        "    print(3);\n"
        "}\n";
    expectOutput("finally_runs_on_normal_completion", src, {"1", "2", "3"});
}

void test_finally_runs_after_caught_exception() {
    std::string src =
        "fn main() {\n"
        "    try {\n"
        "        throw Exception(\"x\");\n"
        "    } catch e: Exception {\n"
        "        print(\"caught\");\n"
        "    } finally {\n"
        "        print(\"cleanup\");\n"
        "    }\n"
        "}\n";
    expectOutput("finally_runs_after_caught_exception", src, {"caught", "cleanup"});
}

void test_finally_runs_when_exception_propagates_past_non_matching_catch() {
    // catch этого try не подходит по типу — finally обязан выполниться,
    // прежде чем исключение пойдёт дальше. Оборачиваем во внешний try,
    // который ловит, чтобы прогон завершился успешно и printedLines()
    // не подменился сообщением об ошибке (см. runSource выше — при
    // vm.errors() он замещает весь output одной строкой; сам факт, что
    // finally выполняется и на пути "нигде не поймано", я уже отдельно
    // проверил вручную через CLI).
    std::string src =
        "mold OtherError : Exception {\n"
        "    OtherError(msg: string) { super(msg); }\n"
        "}\n"
        "fn main() {\n"
        "    try {\n"
        "        try {\n"
        "            throw Exception(\"plain\");\n"
        "        } catch e: OtherError {\n"
        "            print(\"should not happen\");\n"
        "        } finally {\n"
        "            print(\"inner cleanup\");\n"
        "        }\n"
        "    } catch e: Exception {\n"
        "        print(\"outer caught: \" + e.message);\n"
        "    }\n"
        "}\n";
    expectOutput("finally_runs_when_exception_propagates_past_non_matching_catch", src,
                 {"inner cleanup", "outer caught: plain"});
}

// --- Через границу вызова функции ---

void test_exception_from_called_function_caught_by_caller() {
    std::string src =
        "fn risky(n: int) -> int {\n"
        "    if n < 0 {\n"
        "        throw Exception(\"negative\");\n"
        "    }\n"
        "    return n;\n"
        "}\n"
        "fn main() {\n"
        "    try {\n"
        "        print(risky(-1));\n"
        "    } catch e: Exception {\n"
        "        print(\"caught: \" + e.message);\n"
        "    }\n"
        "}\n";
    expectOutput("exception_from_called_function_caught_by_caller", src, {"caught: negative"});
}

void test_rethrow_from_catch_body_caught_by_outer_try() {
    std::string src =
        "fn main() {\n"
        "    try {\n"
        "        try {\n"
        "            throw Exception(\"inner\");\n"
        "        } catch e: Exception {\n"
        "            print(\"inner caught: \" + e.message);\n"
        "            throw Exception(\"rethrown\");\n"
        "        }\n"
        "    } catch e: Exception {\n"
        "        print(\"outer caught: \" + e.message);\n"
        "    }\n"
        "}\n";
    expectOutput("rethrow_from_catch_body_caught_by_outer_try", src, {"inner caught: inner", "outer caught: rethrown"});
}

// --- Ограничения, которые честно отвергаются на этапе компиляции ---

void test_return_inside_try_with_finally_is_rejected() {
    std::string src =
        "fn f() -> int {\n"
        "    try {\n"
        "        return 1;\n"
        "    } finally {\n"
        "        print(\"cleanup\");\n"
        "    }\n"
        "}\n"
        "fn main() { print(f()); }\n";
    expectContaining("return_inside_try_with_finally_is_rejected", src, "ERROR: codegen");
}

void test_return_inside_try_without_finally_is_fine() {
    // Гвард должен срабатывать только при реальном finally — без него
    // return из try/catch работает как обычно (стек фреймов не знает про
    // try вообще, только таблица обработчиков — см. VM::unwindToHandler).
    std::string src =
        "fn f() -> int {\n"
        "    try {\n"
        "        return 1;\n"
        "    } catch e: Exception {\n"
        "        return 2;\n"
        "    }\n"
        "}\n"
        "fn main() { print(f()); }\n";
    expectOutput("return_inside_try_without_finally_is_fine", src, {"1"});
}

// --- GC: исключение — обычный mold-объект в той же куче ---

void test_gc_reclaims_exception_object_after_catch() {
    // Exception, пойманный и использованный, не должен утекать — это
    // обычный ObjInstance под тем же mark-and-sweep, что и любой mold.
    std::string src =
        "fn main() {\n"
        "    let i = 0;\n"
        "    while i < 300 {\n"
        "        try {\n"
        "            throw Exception(\"x\");\n"
        "        } catch e: Exception {\n"
        "            let ignored = e.message;\n"
        "        }\n"
        "        i += 1;\n"
        "    }\n"
        "    print(i);\n"
        "}\n";
    auto result = runSource(src);
    CHECK(result.output == std::vector<std::string>{"300"});
    if (result.liveObjectsAfterRun >= 50) {
        std::cerr << "FAILED: gc_reclaims_exception_object_after_catch — живых объектов: " << result.liveObjectsAfterRun
                  << " (ожидалось значительно меньше 50)\n";
    }
    CHECK(result.liveObjectsAfterRun < 50);
}

} // namespace

int main() {
    test_basic_throw_and_catch();
    test_try_without_throw_runs_normally();
    test_uncaught_exception_is_clean_runtime_error();
    test_exception_message_preserved_through_field();

    test_user_defined_exception_subtype();
    test_catch_by_base_type_catches_subtype();
    test_multiple_catches_pick_most_specific_first_match();
    test_non_matching_catch_propagates_to_outer_try();

    test_finally_runs_on_normal_completion();
    test_finally_runs_after_caught_exception();
    test_finally_runs_when_exception_propagates_past_non_matching_catch();

    test_exception_from_called_function_caught_by_caller();
    test_rethrow_from_catch_body_caught_by_outer_try();

    test_return_inside_try_with_finally_is_rejected();
    test_return_inside_try_without_finally_is_fine();

    test_gc_reclaims_exception_object_after_catch();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

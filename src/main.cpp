#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // SetConsoleOutputCP
#endif

#include "codegen/Compiler.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "prelude/Prelude.h"
#include "repl/Repl.h"
#include "sema/BodyChecker.h"
#include "sema/Sema.h"
#include "vm/VM.h"

namespace {
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("не удалось открыть файл: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Консоль Windows по умолчанию работает в «OEM»-кодировке (CP866/CP437), а исходники, строки Glass и
    // сообщения компилятора — в UTF-8: без переключения кириллица в выводе превращается в «кракозябры».
    // Прежние кодовые страницы возвращаем при выходе, чтобы не портить консоль после себя.
    struct ConsoleUtf8 {
        UINT output = GetConsoleOutputCP();
        UINT input = GetConsoleCP();
        ConsoleUtf8() { SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8); }
        ~ConsoleUtf8() { SetConsoleOutputCP(output); SetConsoleCP(input); }
    } consoleUtf8;
#endif
    if (argc == 1) {
        return glass::runRepl();
    }
    if (argc != 2) {
        std::cerr << "Использование: glassc [файл.glass]\n";
        std::cerr << "  без аргументов — интерактивная консоль (REPL)\n";
        return 1;
    }

    std::string source;
    try {
        source = readFile(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    glass::Lexer lexer(std::move(source));
    auto tokens = lexer.tokenize();

    glass::Parser parser(std::move(tokens));
    glass::Program program = parser.parseProgram();

    if (!parser.errors().empty()) {
        std::cerr << parser.errors().size() << " ошибок разбора:\n";
        for (const auto& err : parser.errors()) std::cerr << "  " << err << "\n";
        return 1;
    }

    glass::prependPrelude(program);  // встроенные типы (Exception, LayoutInfo, ...) — см. prelude/Prelude.h

    glass::Sema sema;
    auto semaErrors = sema.analyze(program);
    if (!semaErrors.empty()) {
        std::cerr << semaErrors.size() << " семантических ошибок (декларации):\n";
        for (const auto& err : semaErrors) std::cerr << "  " << err << "\n";
        return 1;
    }

    glass::BodyChecker bodyChecker(sema);
    auto bodyErrors = bodyChecker.checkProgram(program);
    if (!bodyErrors.empty()) {
        std::cerr << bodyErrors.size() << " ошибок типизации (тела функций):\n";
        for (const auto& err : bodyErrors) std::cerr << "  " << err << "\n";
        return 1;
    }

    glass::Compiler compiler(sema);
    glass::CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(program);
    } catch (const std::exception& e) {
        std::cerr << "Ошибка компиляции в байткод: " << e.what() << "\n";
        std::cerr << "(внутренняя ошибка компилятора — если код использует только return/break/continue вне "
                     "try/catch с finally (см. README «Сознательные упрощения»), это баг, а не ожидаемое "
                     "ограничение)\n";
        return 1;
    }

    glass::VM vm(std::move(compiled));
    int exitCode = vm.run();
    if (!vm.errors().empty()) {
        std::cerr << "Ошибка выполнения: " << vm.errors()[0] << "\n";
    }
    return exitCode;
}





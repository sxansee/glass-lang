#include "repl/Repl.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "codegen/Compiler.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "prelude/Prelude.h"
#include "sema/BodyChecker.h"
#include "sema/Sema.h"
#include "vm/VM.h"

namespace glass {
namespace {

const char* kBanner =
    "Glass REPL\n"
    "  mold/facet/fn (кроме main) копятся как декларации сессии;\n"
    "  любой другой оператор — как очередная строка тела main.\n"
    "  Фрагмент исполняется, как только '{'/'}' сбалансированы.\n"
    "  :show — текущая собранная программа   :reset — очистить сессию\n"
    "  :help — эта справка                   :quit  — выход (или Ctrl+D)\n";

std::vector<Token> tokenizeSource(const std::string& src) {
    Lexer lexer(src);
    return lexer.tokenize();
}

// Баланс '{'/'}' по уже распознанным токенам, а не по сырым символам — так
// скобки внутри строк/комментариев не путают подсчёт (Lexer сам разбирает
// StringLiteral одним токеном и пропускает комментарии).
int braceDelta(const std::vector<Token>& tokens) {
    int depth = 0;
    for (const auto& t : tokens) {
        if (t.type == TokenType::LBrace) ++depth;
        else if (t.type == TokenType::RBrace) --depth;
    }
    return depth;
}

bool isOnlyTrivia(const std::vector<Token>& tokens) {
    for (const auto& t : tokens) {
        if (t.type != TokenType::EndOfFile) return false;
    }
    return true;
}

// VM::run() печатает вывод print() сразу в std::cout (VM.cpp, OpCode::Print)
// — так и должно быть для файлового режима glassc. Но REPL на каждом шаге
// заново исполняет ВСЮ накопленную программу (движок не умеет дописывать код
// в уже запущенную VM), а значит без перехвата весь предыдущий вывод
// переигрывался бы заново на каждом шаге. Подменяем cout на время run() и
// сами решаем, что из захваченного показать — см. printFreshSuffix.
class CoutRedirect {
public:
    explicit CoutRedirect(std::ostream& target) : old_(std::cout.rdbuf(target.rdbuf())) {}
    ~CoutRedirect() { std::cout.rdbuf(old_); }
    CoutRedirect(const CoutRedirect&) = delete;
    CoutRedirect& operator=(const CoutRedirect&) = delete;

private:
    std::streambuf* old_;
};

struct RunOutcome {
    bool ok = false;
    std::vector<std::string> diagnostics;
    std::string output;  // всё, что print() написал бы в консоль за этот прогон целиком
};

// Прогоняет source через весь пайплайн — та же последовательность фаз и та
// же логика ошибок, что в main.cpp (файловый режим), только без завершения
// процесса на первой ошибке.
RunOutcome runSource(const std::string& source) {
    RunOutcome outcome;

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    if (!parser.errors().empty()) {
        outcome.diagnostics = parser.errors();
        return outcome;
    }

    prependPrelude(program);  // встроенные типы (Exception, LayoutInfo, ...) — см. prelude/Prelude.h

    Sema sema;
    auto semaErrors = sema.analyze(program);
    if (!semaErrors.empty()) {
        outcome.diagnostics = semaErrors;
        return outcome;
    }

    BodyChecker bodyChecker(sema);
    auto bodyErrors = bodyChecker.checkProgram(program);
    if (!bodyErrors.empty()) {
        outcome.diagnostics = bodyErrors;
        return outcome;
    }

    Compiler compiler(sema);
    CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(program);
    } catch (const std::exception& e) {
        outcome.diagnostics.push_back(std::string("ошибка компиляции в байткод: ") + e.what() +
                                       " (return/break/continue сквозь try/catch с finally — см. README; "
                                       "любая другая ошибка здесь — баг)");
        return outcome;
    }

    VM vm(std::move(compiled));
    std::ostringstream captured;
    {
        CoutRedirect redirect(captured);
        vm.run();
    }
    outcome.output = captured.str();

    if (!vm.errors().empty()) {
        outcome.diagnostics.push_back("ошибка выполнения: " + vm.errors()[0]);
        return outcome;  // ok остаётся false — фрагмент откатывается вызывающим кодом
    }

    outcome.ok = true;
    return outcome;
}

std::string buildSessionProgram(const std::string& declarations, const std::vector<std::string>& statements) {
    std::string program = declarations;
    program += "\nfn main() {\n";
    for (const auto& stmt : statements) program += stmt;
    program += "}\n";
    return program;
}

// baseline — вывод предыдущего успешного прогона; т.к. сессия только растёт
// (новый код дописывается в конец), он всегда ровно префикс fullOutput.
// Показываем только то, что за этим префиксом — то есть по-настоящему новое.
void printFreshSuffix(const std::string& fullOutput, const std::string& baseline) {
    if (fullOutput.size() <= baseline.size()) return;
    std::cout << fullOutput.substr(baseline.size());
}

} // namespace

int runRepl() {
    std::cout << kBanner;

    std::string declarations;             // накопленные mold/facet/fn (кроме main)
    std::vector<std::string> statements;  // операторы тела синтетического main
    std::string sessionOutput;            // вывод последнего успешного прогона сессии целиком

    std::string buffer;
    bool continuation = false;

    while (true) {
        std::cout << (continuation ? "...     " : "glass>  ");
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;  // EOF (Ctrl+D)
        }

        size_t firstNonSpace = line.find_first_not_of(" \t\r");
        std::string word = (firstNonSpace == std::string::npos) ? "" : line.substr(firstNonSpace);

        if (word == ":cancel" && continuation) {
            buffer.clear();
            continuation = false;
            continue;
        }
        if (!continuation && !word.empty() && word[0] == ':') {
            if (word == ":quit" || word == ":exit") break;
            if (word == ":help") {
                std::cout << kBanner;
                continue;
            }
            if (word == ":reset") {
                declarations.clear();
                statements.clear();
                sessionOutput.clear();
                std::cout << "сессия очищена\n";
                continue;
            }
            if (word == ":show") {
                std::cout << buildSessionProgram(declarations, statements);
                continue;
            }
            std::cout << "неизвестная команда: " << word << " (:help — список)\n";
            continue;
        }

        buffer += line;
        buffer += "\n";

        auto tokens = tokenizeSource(buffer);
        if (braceDelta(tokens) > 0) {
            continuation = true;
            continue;
        }
        continuation = false;

        if (isOnlyTrivia(tokens)) {
            buffer.clear();
            continue;
        }

        TokenType first = tokens[0].type;

        // Пользователь сам написал fn main() — разовый запуск с текущими
        // декларациями сессии. Не трогает declarations/statements: иначе
        // на следующем обычном шаге получился бы конфликт "main уже есть".
        if (first == TokenType::Fn && tokens.size() > 1 && tokens[1].type == TokenType::Identifier &&
            tokens[1].lexeme == "main") {
            auto result = runSource(declarations + "\n" + buffer);
            std::cout << result.output;
            for (const auto& d : result.diagnostics) std::cout << "  " << d << "\n";
            if (result.ok) std::cout << "(разовый запуск — своя main не сохраняется в сессии)\n";
            buffer.clear();
            continue;
        }

        bool isDecl = (first == TokenType::Mold || first == TokenType::Facet || first == TokenType::Fn);

        std::string savedDeclarations = declarations;
        std::vector<std::string> savedStatements = statements;
        if (isDecl) {
            declarations += "\n" + buffer;
        } else {
            statements.push_back(buffer);
        }

        auto result = runSource(buildSessionProgram(declarations, statements));
        printFreshSuffix(result.output, sessionOutput);
        if (result.ok) {
            sessionOutput = result.output;
        } else {
            for (const auto& d : result.diagnostics) std::cout << "  " << d << "\n";
            declarations = savedDeclarations;
            statements = savedStatements;
        }

        buffer.clear();
    }

    std::cout << "пока!\n";
    return 0;
}

} // namespace glass

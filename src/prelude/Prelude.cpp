#include "Prelude.h"

#include <utility>

#include "../lexer/Lexer.h"
#include "../parser/Parser.h"

namespace glass {
namespace {

// Exception — реальный mold (не спецкейс): наследование, super(), catch по
// базовому типу — всё работает как для любого пользовательского mold'а.
//
// LayoutInfo/VTableInfo/HeapSnapshot — только ПОЛЯ, без конструктора: их
// никогда не строит пользовательский код через ИмяТипа(...) — VM сама
// выделяет и заполняет их поля напрямую в обработчиках OpCode::Layout/
// Vtable/Heap (см. VM.cpp), используя эти mold'ы просто как готовый,
// уже протестированный способ описать "объект с такими-то полями в куче
// под тем же GC" — вместо того чтобы городить для интроспекции отдельный,
// не-mold'овый вид значения.
const char* kPreludeSource =
    "mold Exception {\n"
    "    let message: string\n"
    "\n"
    "    Exception(msg: string) {\n"
    "        message = msg\n"
    "    }\n"
    "}\n"
    "\n"
    "mold LayoutInfo {\n"
    "    let className: string\n"
    "    let fieldNames: [string]\n"
    "    let fieldOffsets: [int]\n"
    "    let fieldSizes: [int]\n"
    "}\n"
    "\n"
    "mold VTableInfo {\n"
    "    let className: string\n"
    "    let methodNames: [string]\n"
    "    let implementingClasses: [string]\n"
    "}\n"
    "\n"
    "mold HeapSnapshot {\n"
    "    let objectCount: int\n"
    "    let classNames: [string]\n"
    "    let addresses: [int]\n"
    "}\n";

// Строки пролога — отрицательные (-1000, -999, ...): ни одна из них не
// совпадает со строкой пользовательского исходника (они с 1), поэтому
// отладчик (точки останова, шаги, стек вызовов) никогда не принимает код
// пролога за код пользователя — см. VM::isUserLine. Для самого языка
// ничего не меняется: номера строк — только в диагностике и отладке.
constexpr int kPreludeFirstLine = -1000;

}  // namespace

void prependPrelude(Program& program) {
    Lexer lexer(kPreludeSource, kPreludeFirstLine);
    Parser parser(lexer.tokenize());
    Program preludeProgram = parser.parseProgram();
    // Пролог — фиксированный, доверенный исходник: если он не разбирается,
    // это баг в самом прологе, а не во входных данных пользователя, так
    // что parser.errors() здесь намеренно не проверяются.
    program.declarations.insert(program.declarations.begin(),
                                 std::make_move_iterator(preludeProgram.declarations.begin()),
                                 std::make_move_iterator(preludeProgram.declarations.end()));
}

}  // namespace glass

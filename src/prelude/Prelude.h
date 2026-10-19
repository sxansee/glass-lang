#pragma once

#include "../ast/Decl.h"

namespace glass {

// Встроенные типы, которые не пишутся пользователем на Glass, но реальны
// для Sema/Compiler/VM — участвуют в обычной цепочке наследования и
// компилируются как любой другой mold, без спецкейсов в тайпчекере:
//
//  - Exception — mold MyError : Exception работает как любое наследование
//    mold : mold; catch по базовому типу ловит потомков (см.
//    ClassInfo::ancestry в bytecode/ClassInfo.h).
//  - LayoutInfo/VTableInfo/HeapSnapshot — носители данных для ~layout/
//    ~vtable/~heap (раздел 8 grammar.md): просто molds с полями, которые
//    VM заполняет напрямую в обработчиках OpCode::Layout/Vtable/Heap
//    (см. vm/VM.cpp), а не через обычный вызов конструктора.
//
// BodyChecker до сих пор трактует НЕИЗВЕСТНЫЕ типы (например, в catch или
// в позиции, ожидающей mold) как "непрозрачные" (см. BodyChecker.h) — но
// раз эти типы реально объявлены, для них этот путь просто не сработает:
// имя резолвится как обычный mold.
//
// Дописывает декларации в НАЧАЛО program.declarations — до Sema, чтобы
// дальше это были обычные, полноценно проверенные molds.
void prependPrelude(Program& program);

} // namespace glass

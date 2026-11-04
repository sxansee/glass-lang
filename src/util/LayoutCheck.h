#pragma once
// «Сверка раскладки» — защита от наполовину пересобранной сборки.
//
// Структуры AST и байткода (Expr.h, Decl.h, Chunk.h ...) общие для нескольких единиц трансляции: парсер СОЗДАЁТ узлы,
// чекер и компилятор их ЧИТАЮТ. Если в заголовке появилось поле, а часть объектных файлов не пересобралась (так бывает,
// когда файлы разворачивают из архива с сохранением старых дат изменения: сборка решает, что исходники не новее
// объектных файлов), разные части программы по-разному понимают одну и ту же структуру. Результат — чтение за границей
// объекта, «мусорные» флаги и падения далеко от причины.
//
// Поэтому каждая единица трансляции считает отпечаток размеров структур ПО СВОИМ заголовкам (см. ast/AstLayout.h и
// bytecode/BytecodeLayout.h), а checkBuildConsistency() сравнивает отпечатки: расхождение — понятное сообщение
// «пересоберите проект целиком» вместо загадочного поведения. Вызывается при создании сеанса компиляции.

#include <cstddef>
#include <initializer_list>
#include <string>

namespace glass {

inline unsigned long long layoutOf(std::initializer_list<unsigned long long> values) {
    unsigned long long hash = 1469598103934665603ULL;  // FNV-1a
    for (unsigned long long value : values) hash = (hash ^ value) * 1099511628211ULL;
    return hash;
}

// Каждая функция определена в СВОЁМ .cpp (макросы GLASS_DEFINE_*_LAYOUT) и считается по заголовкам этого .cpp.
unsigned long long parserAstLayout();
unsigned long long semaAstLayout();
unsigned long long checkerAstLayout();
unsigned long long compilerAstLayout();
unsigned long long compilerBytecodeLayout();
unsigned long long vmBytecodeLayout();

// Пустая строка — все части собраны по одним и тем же заголовкам. Иначе — сообщение для пользователя.
inline std::string checkBuildConsistency() {
    const unsigned long long ast = parserAstLayout();
    const bool astConsistent = ast == semaAstLayout() && ast == checkerAstLayout() && ast == compilerAstLayout();
    const bool bytecodeConsistent = compilerBytecodeLayout() == vmBytecodeLayout();
    if (astConsistent && bytecodeConsistent) return "";
    return std::string("Glass собран из несогласованных частей: общие заголовки (") + (astConsistent ? "" : "дерево программы") +
           (!astConsistent && !bytecodeConsistent ? ", " : "") + (bytecodeConsistent ? "" : "байткод") +
           ") изменились, а часть объектных файлов осталась от прежней версии. "
           "Удалите папку сборки (build) целиком и соберите проект заново — иначе программа будет вести себя непредсказуемо.";
}

}  // namespace glass

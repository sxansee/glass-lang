#pragma once
// Встроенные функции и методы Glass. Одна таблица на три слоя: BodyChecker типизирует вызовы
// по именам отсюда, Compiler выпускает OpCode::CallBuiltin с номером, VM исполняет (VMBuiltins.cpp).
// Header-only — чтобы sema/codegen/vm не зависели друг от друга ради таблицы имён.

#include <cstddef>
#include <string>

namespace glass {

enum class Builtin : int {
    // --- функции ---
    Len, Str, ToInt, ToDouble, ToBool, Range, Abs, Min, Max, Sum, Sqrt, Pow, Floor, Ceil, Round,
    // --- методы массивов (первый аргумент — сам массив) ---
    ArrAppend, ArrPop, ArrInsert, ArrRemove, ArrContains, ArrIndexOf, ArrReverse, ArrSort, ArrClear, ArrJoin,
    // --- методы строк (первый аргумент — сама строка) ---
    StrUpper, StrLower, StrTrim, StrContains, StrStartsWith, StrEndsWith, StrIndexOf, StrReplace, StrSplit,
    StrRepeat,
};

struct BuiltinDef {
    const char* name;
    Builtin id;
};

namespace builtin_detail {
template <size_t N>
const BuiltinDef* find(const BuiltinDef (&table)[N], const std::string& name) {
    for (const auto& def : table) {
        if (name == def.name) return &def;
    }
    return nullptr;
}
}  // namespace builtin_detail

// int/double/bool/string — тоже имена функций: в парсере это ключевые слова-типы, но запись
// int("42") превращается в вызов обычной функции с таким именем.
inline const BuiltinDef* findBuiltinFunction(const std::string& name) {
    static const BuiltinDef table[] = {
        {"len", Builtin::Len},       {"str", Builtin::Str},     {"string", Builtin::Str},
        {"int", Builtin::ToInt},     {"double", Builtin::ToDouble}, {"bool", Builtin::ToBool},
        {"range", Builtin::Range},   {"abs", Builtin::Abs},     {"min", Builtin::Min},
        {"max", Builtin::Max},       {"sum", Builtin::Sum},     {"sqrt", Builtin::Sqrt},
        {"pow", Builtin::Pow},       {"floor", Builtin::Floor}, {"ceil", Builtin::Ceil},
        {"round", Builtin::Round},
    };
    return builtin_detail::find(table, name);
}

inline const BuiltinDef* findArrayMethod(const std::string& name) {
    static const BuiltinDef table[] = {
        {"append", Builtin::ArrAppend},     {"pop", Builtin::ArrPop},         {"insert", Builtin::ArrInsert},
        {"remove", Builtin::ArrRemove},     {"contains", Builtin::ArrContains}, {"indexOf", Builtin::ArrIndexOf},
        {"reverse", Builtin::ArrReverse},   {"sort", Builtin::ArrSort},       {"clear", Builtin::ArrClear},
        {"join", Builtin::ArrJoin},
    };
    return builtin_detail::find(table, name);
}

inline const BuiltinDef* findStringMethod(const std::string& name) {
    static const BuiltinDef table[] = {
        {"upper", Builtin::StrUpper},         {"lower", Builtin::StrLower},     {"trim", Builtin::StrTrim},
        {"contains", Builtin::StrContains},   {"startsWith", Builtin::StrStartsWith},
        {"endsWith", Builtin::StrEndsWith},   {"indexOf", Builtin::StrIndexOf}, {"replace", Builtin::StrReplace},
        {"split", Builtin::StrSplit},         {"repeat", Builtin::StrRepeat},
    };
    return builtin_detail::find(table, name);
}

}  // namespace glass

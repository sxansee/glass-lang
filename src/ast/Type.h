#pragma once

#include <memory>
#include <string>
#include <vector>

namespace glass {

// Синтаксическое представление типа (раздел 5 grammar.md). Типы ещё не
// разрешены семантически — тайпчекер позже сопоставит имена с реальными
// mold/facet или сообщит об ошибке.
struct TypeNode {
    enum class Kind { Primitive, Named, Array };

    Kind kind = Kind::Named;
    std::string name;                       // Primitive: int/double/bool/string; Named: имя типа
    std::vector<TypeNode> genericArgs;       // Named<T, U, ...>
    std::shared_ptr<TypeNode> elementType;   // Array: [T]

    int line = 0;
    int column = 0;
};

std::string typeToString(const TypeNode& t);

} // namespace glass

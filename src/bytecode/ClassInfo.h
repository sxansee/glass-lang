#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace glass {

enum class FieldKind { Int, Double, Bool, Reference };

struct FieldSlot {
    std::string name;
    int offset = 0;
    FieldKind kind = FieldKind::Reference;
};

// Дескриптор класса (mold) времени исполнения — то, на что указывает
// каждый ObjInstance. vtable — по имени метода, а не по фиксированному
// слоту: facet может реализовываться molds с разным набором и порядком
// виртуальных методов, единой на всех позиционной нумерации слотов не
// получить без отдельной таблицы на facet (itable), а строковый поиск —
// простое и корректное решение для этой фазы (см. README).
//
// Полей 'base'/указателей на другие ClassInfo намеренно нет: fields и
// vtable уже полностью "развёрнуты" (включают унаследованное) на момент
// построения — во время исполнения обходить цепочку наследования не
// нужно, а хранить сырой указатель было бы риском (ClassInfo копируются
// в CompiledProgram::classes, значит любой указатель на промежуточную
// структуру построения быстро стал бы висячим).
struct ClassInfo {
    std::string name;

    std::vector<FieldSlot> fields;  // включая унаследованные, по смещению
    std::unordered_map<std::string, int> vtable;  // имя метода -> индекс CompiledFunction

    // Имена себя, всех предков по mold-цепочке и всех facet'ов, которые
    // реализует эта цепочка (см. Compiler::buildClassInfo). Раньше в
    // ClassInfo сознательно не было ничего о наследовании — во время
    // исполнения было незачем (fields/vtable уже "развёрнуты"). Для catch
    // по типу (catch e: Exception должен ловить и потомков Exception) это
    // уже не так: нужен набор "чем является объект" для runtime-проверки
    // совпадения типа — см. VM::exceptionIsInstanceOf.
    std::vector<std::string> ancestry;

    int fieldInitFunctionIndex = -1;   // всегда есть, даже если тело пустое
    int constructorFunctionIndex = -1; // -1 = явного конструктора нет

    const FieldSlot* findField(const std::string& fieldName) const {
        for (auto& f : fields) {
            if (f.name == fieldName) return &f;
        }
        return nullptr;
    }
};

} // namespace glass

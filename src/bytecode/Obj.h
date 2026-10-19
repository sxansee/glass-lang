#pragma once

#include <vector>

#include "Value.h"

namespace glass {

struct ClassInfo;

// Экземпляр mold'а в куче. Поля — плоский массив по смещению (базовые
// сначала, свои дальше — совместимость layout'а вдоль цепочки наследования,
// как в C++/Java/C#). 'marked'/'nextAllocated' — служебные поля для
// mark-and-sweep GC (VM), не части языка.
struct ObjInstance {
    const ClassInfo* classInfo = nullptr;
    std::vector<Value> fields;

    bool marked = false;
    ObjInstance* nextAllocated = nullptr;
};

// Массив в куче — тот же принцип, что ObjInstance (плоское хранилище +
// intrusive-список для sweep), только элементы не по смещению класса, а
// по числовому индексу. Отдельный тип (не переиспользуем ObjInstance) —
// чтобы не городить фиктивный ClassInfo для массивов; VM ведёт для него
// свой intrusive-список, но тот же liveObjectCount_ и тот же mark-and-sweep
// цикл (см. VM::markValue/collectGarbage) — с точки зрения GC это такой же
// объект в куче, как mold-инстанс.
struct ObjArray {
    std::vector<Value> elements;

    bool marked = false;
    ObjArray* nextAllocated = nullptr;
};

} // namespace glass

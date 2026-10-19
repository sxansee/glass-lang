#include "Value.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

#include "ClassInfo.h"
#include "Obj.h"

namespace glass {
namespace {

std::string stringField(ObjInstance* o, const std::string& fieldName) {
    const FieldSlot* slot = o->classInfo->findField(fieldName);
    if (!slot) return "";
    const Value& v = o->fields[static_cast<size_t>(slot->offset)];
    return isString(v) ? *std::get<std::shared_ptr<std::string>>(v) : "";
}

const std::vector<Value>* arrayField(ObjInstance* o, const std::string& fieldName) {
    const FieldSlot* slot = o->classInfo->findField(fieldName);
    if (!slot) return nullptr;
    const Value& v = o->fields[static_cast<size_t>(slot->offset)];
    if (!isArray(v)) return nullptr;
    ObjArray* arr = std::get<ObjArray*>(v);
    return arr ? &arr->elements : nullptr;
}

long long intField(ObjInstance* o, const std::string& fieldName) {
    const FieldSlot* slot = o->classInfo->findField(fieldName);
    if (!slot) return 0;
    const Value& v = o->fields[static_cast<size_t>(slot->offset)];
    return isInt(v) ? std::get<long long>(v) : 0;
}

// print() для LayoutInfo/VTableInfo/HeapSnapshot (см. prelude/Prelude.h,
// vm/VM.cpp: OpCode::Layout/Vtable/Heap) — читаемый формат вместо общего
// "<object @...>": весь смысл интроспекции — увидеть результат сразу, не
// вручную разбирая поля. Названия полей этих трёх типов знает только эта
// функция; если prelude когда-нибудь разойдётся с ней, findField()==nullptr
// просто не покажет соответствующий кусок, а не уронит print().
//
// Пустая строка — сигнал "это не один из трёх типов, используй обычный
// формат" (см. вызывающий код ниже) — безопасно как маркер: у всех трёх
// формат всегда начинается с непустого префикса ("layout(...):" и т.п.),
// даже когда полей/элементов ноль.
std::string describeIntrospectionObject(ObjInstance* obj) {
    const std::string& className = obj->classInfo->name;
    std::ostringstream oss;

    if (className == "LayoutInfo") {
        auto* names = arrayField(obj, "fieldNames");
        auto* offsets = arrayField(obj, "fieldOffsets");
        auto* sizes = arrayField(obj, "fieldSizes");
        oss << "layout(" << stringField(obj, "className") << "):";
        if (names) {
            for (size_t i = 0; i < names->size(); ++i) {
                oss << (i == 0 ? " " : ", ") << valueToDisplayString((*names)[i]) << "@"
                    << (offsets && i < offsets->size() ? valueToDisplayString((*offsets)[i]) : "?") << " ("
                    << (sizes && i < sizes->size() ? valueToDisplayString((*sizes)[i]) : "?") << " байт)";
            }
        }
        return oss.str();
    }

    if (className == "VTableInfo") {
        auto* names = arrayField(obj, "methodNames");
        auto* impls = arrayField(obj, "implementingClasses");
        oss << "vtable(" << stringField(obj, "className") << "):";
        if (names) {
            for (size_t i = 0; i < names->size(); ++i) {
                std::string method = valueToDisplayString((*names)[i]);
                oss << (i == 0 ? " " : ", ") << method << " -> "
                    << (impls && i < impls->size() ? valueToDisplayString((*impls)[i]) : "?") << "." << method;
            }
        }
        return oss.str();
    }

    if (className == "HeapSnapshot") {
        auto* names = arrayField(obj, "classNames");
        auto* addrs = arrayField(obj, "addresses");
        oss << "heap(" << intField(obj, "objectCount") << " объект(ов)):";
        if (names) {
            for (size_t i = 0; i < names->size(); ++i) {
                oss << (i == 0 ? " " : ", ") << valueToDisplayString((*names)[i]) << "@"
                    << (addrs && i < addrs->size() ? valueToDisplayString((*addrs)[i]) : "?");
            }
        }
        return oss.str();
    }

    return "";
}

}  // namespace

std::string formatDouble(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.15g", d);
    std::string text = buffer;
    if (text.find_first_of(".e") == std::string::npos) text += ".0";   // 3 → 3.0
    return text;
}

namespace {

std::string displayImpl(const Value& v, int depth, bool quoteStrings);

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

}  // namespace

std::string valueToDisplayString(const Value& v) { return displayImpl(v, 0, false); }

namespace {

std::string displayImpl(const Value& v, int depth, bool quoteStrings) {
    if (isInt(v)) return std::to_string(std::get<long long>(v));
    if (isDouble(v)) return formatDouble(std::get<double>(v));
    if (isBool(v)) return std::get<bool>(v) ? "true" : "false";
    if (isNull(v)) return "null";
    if (isString(v)) {
        const std::string& text = *std::get<std::shared_ptr<std::string>>(v);
        return quoteStrings ? quote(text) : text;
    }
    if (isObject(v)) {
        ObjInstance* obj = std::get<ObjInstance*>(v);
        if (obj) {
            std::string special = describeIntrospectionObject(obj);
            if (!special.empty()) return special;
        }
        if (!obj) return "null";
        // Класс@0x…: как Object.toString() в Java. Адрес выводим сами в hex с префиксом 0x —
        // потоковый вывод указателя на MSVC печатает его без «0x» и в верхнем регистре.
        std::ostringstream oss;
        oss << obj->classInfo->name << "@0x" << std::hex << reinterpret_cast<uintptr_t>(obj);
        return oss.str();
    }
    if (isArray(v)) {
        ObjArray* arr = std::get<ObjArray*>(v);
        if (!arr) return "null";
        if (depth >= 8) return "[...]";   // вложенность или цикл (массив, содержащий сам себя)
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << displayImpl(arr->elements[i], depth + 1, true);   // строки внутри массива — в кавычках
        }
        oss << "]";
        return oss.str();
    }
    return "?";
}

}  // namespace

bool valuesEqual(const Value& a, const Value& b) {
    if (isInt(a) && isInt(b)) return std::get<long long>(a) == std::get<long long>(b);
    if ((isInt(a) || isDouble(a)) && (isInt(b) || isDouble(b))) return toDouble(a) == toDouble(b);
    if (isBool(a) && isBool(b)) return std::get<bool>(a) == std::get<bool>(b);
    if (isNull(a) && isNull(b)) return true;
    if (isString(a) && isString(b)) {
        return *std::get<std::shared_ptr<std::string>>(a) == *std::get<std::shared_ptr<std::string>>(b);
    }
    if (isObject(a) && isObject(b)) return std::get<ObjInstance*>(a) == std::get<ObjInstance*>(b);
    // Массивы — ссылочный тип (grammar.md, раздел 5): '==' сравнивает
    // идентичность, не содержимое, как и для объектов выше.
    if (isArray(a) && isArray(b)) return std::get<ObjArray*>(a) == std::get<ObjArray*>(b);
    return false;
}

} // namespace glass

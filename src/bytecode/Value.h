#pragma once

#include <memory>
#include <string>
#include <variant>

namespace glass {

struct ObjInstance;
struct ObjArray;

// Значение на стеке VM. monostate — null. Строки — через shared_ptr (не
// участвуют в GC, отдельная, более простая схема жизни). ObjInstance* —
// объект mold'а в куче, под управлением mark-and-sweep GC самой VM (см.
// vm/VM.h) — сырой указатель, не shared_ptr, специально: GC решает,
// когда объект уничтожать, а не подсчёт ссылок. ObjArray* — массив, та же
// схема (сырой указатель, тот же GC), см. bytecode/Obj.h.
using Value =
    std::variant<long long, double, bool, std::monostate, std::shared_ptr<std::string>, ObjInstance*, ObjArray*>;

inline bool isInt(const Value& v) { return std::holds_alternative<long long>(v); }
inline bool isDouble(const Value& v) { return std::holds_alternative<double>(v); }
inline bool isBool(const Value& v) { return std::holds_alternative<bool>(v); }
inline bool isNull(const Value& v) { return std::holds_alternative<std::monostate>(v); }
inline bool isString(const Value& v) { return std::holds_alternative<std::shared_ptr<std::string>>(v); }
inline bool isObject(const Value& v) { return std::holds_alternative<ObjInstance*>(v); }
inline bool isArray(const Value& v) { return std::holds_alternative<ObjArray*>(v); }

inline double toDouble(const Value& v) {
    if (isInt(v)) return static_cast<double>(std::get<long long>(v));
    if (isDouble(v)) return std::get<double>(v);
    return 0.0;
}

// Число с плавающей точкой для людей: до 15 значащих цифр и ВСЕГДА с точкой или экспонентой
// (3.0, 2.5, 1e+20) — чтобы double был виден как double, а не как int. inf/nan — "inf", "-inf", "nan".
std::string formatDouble(double d);

// Как print показывает значение: строка — как есть; массив — [1, 2, 3], строки внутри в кавычках.
std::string valueToDisplayString(const Value& v);
bool valuesEqual(const Value& a, const Value& b);

} // namespace glass

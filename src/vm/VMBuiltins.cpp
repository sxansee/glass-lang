// Встроенные функции и методы Glass (len, str, int, range, xs.append, s.upper, ...) и срезы.
// Это часть VM — отдельный файл только чтобы VM.cpp не разрастался. Таблица имён и номеров —
// bytecode/Builtins.h; типизирует вызовы BodyChecker, выпускает OpCode::CallBuiltin Compiler.
//
// GC: аргументы снимаются со стека ДО выполнения, а сборка мусора запускается только после того,
// как результат положен на стек (см. maybeCollectGarbage) — построение массивов безопасно.

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "../bytecode/Builtins.h"
#include "../util/Utf8.h"
#include "VM.h"

namespace glass {
namespace {

const std::string& strOf(const Value& v) { return *std::get<std::shared_ptr<std::string>>(v); }
Value makeString(std::string s) { return Value(std::make_shared<std::string>(std::move(s))); }
bool isNumber(const Value& v) { return isInt(v) || isDouble(v); }

constexpr size_t kMaxProducedSize = 100u * 1000u * 1000u;  // защита от range(10**12) и "x".repeat(10**12)

// Границы среза по правилам Python (slice.indices): отрицательные считаются от конца, выход за границы
// обрезается, шаг может быть отрицательным. Обход: step > 0 — пока i < stop; step < 0 — пока i > stop.
struct SliceRange {
    long long start = 0;
    long long stop = 0;
    long long step = 1;
};

bool normalizeSlice(long long n, bool hasStart, long long start, bool hasStop, long long stop, bool hasStep,
                    long long step, SliceRange& out, std::string& error) {
    if (hasStep && step == 0) {
        error = "шаг среза не может быть равен 0";
        return false;
    }
    long long st = hasStep ? step : 1;
    long long lower = st > 0 ? 0 : -1;
    long long upper = st > 0 ? n : n - 1;
    auto clamp = [&](long long v) {
        if (v < 0) {
            v += n;
            if (v < lower) v = lower;
        } else if (v > upper) {
            v = upper;
        }
        return v;
    };
    out.start = hasStart ? clamp(start) : (st > 0 ? lower : upper);
    out.stop = hasStop ? clamp(stop) : (st > 0 ? upper : lower);
    out.step = st;
    return true;
}

// Разбор числа из строки: без пробелов по краям (их срезаем), только цифры/знак/точка/экспонента.
std::string trimAscii(const std::string& s) {
    size_t b = 0, e = s.size();
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    while (b < e && isSpace(s[b])) b++;
    while (e > b && isSpace(s[e - 1])) e--;
    return s.substr(b, e - b);
}

bool parseInt(const std::string& raw, long long& out) {
    std::string text = trimAscii(raw);
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parseDouble(const std::string& raw, double& out) {
    std::string text = trimAscii(raw);
    if (text.empty()) return false;
    for (char c : text) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) return false;
    }
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

}  // namespace

bool VM::doSlice(const Frame& frame, int present) {
    long long step = 1, stop = 0, start = 0;
    auto popInt = [&](long long& out) {
        Value v = pop();
        out = isInt(v) ? std::get<long long>(v) : 0;
    };
    if (present & 4) popInt(step);
    if (present & 2) popInt(stop);
    if (present & 1) popInt(start);
    Value target = pop();

    SliceRange r;
    std::string error;

    if (isString(target)) {
        const std::string& s = strOf(target);
        const bool ascii = utf8::isAscii(s);
        std::vector<size_t> b;
        if (!ascii) b = utf8::boundaries(s);
        long long n = ascii ? static_cast<long long>(s.size()) : static_cast<long long>(b.size()) - 1;
        if (!normalizeSlice(n, present & 1, start, present & 2, stop, present & 4, step, r, error)) {
            runtimeError(frame, error);
            return false;
        }
        std::string out;
        auto take = [&](long long i) {
            if (ascii) out += s[static_cast<size_t>(i)];
            else out.append(s, b[static_cast<size_t>(i)], b[static_cast<size_t>(i) + 1] - b[static_cast<size_t>(i)]);
        };
        if (r.step > 0) {
            for (long long i = r.start; i < r.stop; i += r.step) take(i);
        } else {
            for (long long i = r.start; i > r.stop; i += r.step) take(i);
        }
        push(makeString(std::move(out)));
        return true;
    }

    if (isArray(target) && std::get<ObjArray*>(target) != nullptr) {
        ObjArray* arr = std::get<ObjArray*>(target);
        long long n = static_cast<long long>(arr->elements.size());
        if (!normalizeSlice(n, present & 1, start, present & 2, stop, present & 4, step, r, error)) {
            runtimeError(frame, error);
            return false;
        }
        std::vector<Value> elems;
        if (r.step > 0) {
            for (long long i = r.start; i < r.stop; i += r.step) elems.push_back(arr->elements[static_cast<size_t>(i)]);
        } else {
            for (long long i = r.start; i > r.stop; i += r.step) elems.push_back(arr->elements[static_cast<size_t>(i)]);
        }
        ObjArray* result = allocateArray(std::move(elems));
        push(Value(result));
        maybeCollectGarbage();
        return true;
    }

    runtimeError(frame, "срез на null");
    return false;
}

bool VM::callBuiltin(const Frame& frame, int id, int argc) {
    std::vector<Value> args(stack_.end() - argc, stack_.end());
    stack_.resize(stack_.size() - static_cast<size_t>(argc));

    auto fail = [&](const std::string& message) {
        runtimeError(frame, message);
        return false;
    };
    auto done = [&](Value result) {
        push(std::move(result));
        return true;
    };
    auto doneNull = [&]() { return done(Value{std::monostate{}}); };
    auto integer = [](long long v) { return Value(v); };

    switch (static_cast<Builtin>(id)) {
        // ------------------------------------------------------------------ функции
        case Builtin::Len: {
            if (isString(args[0])) {
                const std::string& s = strOf(args[0]);
                return done(integer(static_cast<long long>(utf8::isAscii(s) ? s.size() : utf8::length(s))));
            }
            if (isArray(args[0]) && std::get<ObjArray*>(args[0])) {
                return done(integer(static_cast<long long>(std::get<ObjArray*>(args[0])->elements.size())));
            }
            return fail("len() от null");
        }
        case Builtin::Str:
            return done(makeString(valueToDisplayString(args[0])));

        case Builtin::ToInt: {
            const Value& v = args[0];
            if (isInt(v)) return done(v);
            if (isBool(v)) return done(integer(std::get<bool>(v) ? 1 : 0));
            if (isDouble(v)) {
                double d = std::get<double>(v);
                if (!std::isfinite(d) || d >= 9223372036854775808.0 || d < -9223372036854775808.0) {
                    return fail("не удалось преобразовать " + valueToDisplayString(v) + " в int: значение вне диапазона");
                }
                return done(integer(static_cast<long long>(d)));  // отбрасывает дробную часть (к нулю)
            }
            if (isString(v)) {
                long long out = 0;
                if (!parseInt(strOf(v), out)) return fail("не удалось преобразовать строку \"" + strOf(v) + "\" в int");
                return done(integer(out));
            }
            return fail("int() не умеет преобразовывать null");
        }
        case Builtin::ToDouble: {
            const Value& v = args[0];
            if (isDouble(v)) return done(v);
            if (isInt(v)) return done(Value(static_cast<double>(std::get<long long>(v))));
            if (isBool(v)) return done(Value(std::get<bool>(v) ? 1.0 : 0.0));
            if (isString(v)) {
                double out = 0;
                if (!parseDouble(strOf(v), out)) return fail("не удалось преобразовать строку \"" + strOf(v) + "\" в double");
                return done(Value(out));
            }
            return fail("double() не умеет преобразовывать null");
        }
        case Builtin::ToBool: {
            const Value& v = args[0];
            if (isBool(v)) return done(v);
            if (isInt(v)) return done(Value(std::get<long long>(v) != 0));
            if (isDouble(v)) return done(Value(std::get<double>(v) != 0.0));
            if (isString(v)) {
                std::string t = lowerAscii(trimAscii(strOf(v)));
                if (t == "true") return done(Value(true));
                if (t == "false") return done(Value(false));
                return fail("не удалось преобразовать строку \"" + strOf(v) + "\" в bool (ожидалось true или false)");
            }
            return fail("bool() не умеет преобразовывать null");
        }
        case Builtin::Range: {
            long long start = 0, stop = 0, step = 1;
            auto asInt = [](const Value& v) { return isInt(v) ? std::get<long long>(v) : 0; };
            if (args.size() == 1) {
                stop = asInt(args[0]);
            } else {
                start = asInt(args[0]);
                stop = asInt(args[1]);
                if (args.size() == 3) step = asInt(args[2]);
            }
            if (step == 0) return fail("шаг range() не может быть равен 0");
            long long count = 0;
            if (step > 0 && stop > start) count = (stop - start + step - 1) / step;
            if (step < 0 && stop < start) count = (start - stop + (-step) - 1) / (-step);
            if (static_cast<size_t>(count) > kMaxProducedSize) return fail("range() слишком большой");
            std::vector<Value> elems;
            elems.reserve(static_cast<size_t>(count));
            for (long long i = 0, v = start; i < count; i++, v += step) elems.push_back(integer(v));
            ObjArray* arr = allocateArray(std::move(elems));
            push(Value(arr));
            maybeCollectGarbage();
            return true;
        }
        case Builtin::Abs: {
            const Value& v = args[0];
            if (isInt(v)) {
                long long x = std::get<long long>(v);
                if (x == std::numeric_limits<long long>::min()) return fail("abs(): переполнение int");
                return done(integer(x < 0 ? -x : x));
            }
            return done(Value(std::fabs(toDouble(v))));
        }
        case Builtin::Min:
        case Builtin::Max: {
            const bool wantMin = static_cast<Builtin>(id) == Builtin::Min;
            auto better = [&](const Value& a, const Value& b) {   // true, если a строго лучше b
                if (isInt(a) && isInt(b)) return wantMin ? std::get<long long>(a) < std::get<long long>(b)
                                                         : std::get<long long>(a) > std::get<long long>(b);
                return wantMin ? toDouble(a) < toDouble(b) : toDouble(a) > toDouble(b);
            };
            if (args.size() == 2) return done(better(args[1], args[0]) ? args[1] : args[0]);
            if (!isArray(args[0]) || !std::get<ObjArray*>(args[0])) return fail(std::string(wantMin ? "min" : "max") + "() от null");
            auto& elems = std::get<ObjArray*>(args[0])->elements;
            if (elems.empty()) return fail(std::string(wantMin ? "min" : "max") + "() от пустого массива");
            Value best = elems[0];
            for (size_t i = 1; i < elems.size(); i++) {
                if (better(elems[i], best)) best = elems[i];
            }
            return done(best);
        }
        case Builtin::Sum: {
            if (!isArray(args[0]) || !std::get<ObjArray*>(args[0])) return fail("sum() от null");
            auto& elems = std::get<ObjArray*>(args[0])->elements;
            bool anyDouble = false;
            for (const Value& v : elems) anyDouble = anyDouble || isDouble(v);
            if (anyDouble) {
                double total = 0;
                for (const Value& v : elems) total += toDouble(v);
                return done(Value(total));
            }
            long long total = 0;
            for (const Value& v : elems) total += isInt(v) ? std::get<long long>(v) : 0;
            return done(integer(total));
        }
        case Builtin::Sqrt: {
            double d = toDouble(args[0]);
            if (d < 0) return fail("sqrt() от отрицательного числа");
            return done(Value(std::sqrt(d)));
        }
        case Builtin::Pow:
            return done(Value(std::pow(toDouble(args[0]), toDouble(args[1]))));
        case Builtin::Floor:
        case Builtin::Ceil:
        case Builtin::Round: {
            if (isInt(args[0])) return done(args[0]);
            double d = toDouble(args[0]);
            double r = static_cast<Builtin>(id) == Builtin::Floor ? std::floor(d)
                     : static_cast<Builtin>(id) == Builtin::Ceil  ? std::ceil(d)
                                                                  : std::round(d);   // .5 — от нуля: round(2.5) == 3
            if (!std::isfinite(r) || r >= 9223372036854775808.0 || r < -9223372036854775808.0) {
                return fail("результат вне диапазона int");
            }
            return done(integer(static_cast<long long>(r)));
        }

        // ------------------------------------------------------------------ методы массивов
        case Builtin::ArrAppend:
        case Builtin::ArrPop:
        case Builtin::ArrInsert:
        case Builtin::ArrRemove:
        case Builtin::ArrContains:
        case Builtin::ArrIndexOf:
        case Builtin::ArrReverse:
        case Builtin::ArrSort:
        case Builtin::ArrClear:
        case Builtin::ArrJoin: {
            if (!isArray(args[0]) || !std::get<ObjArray*>(args[0])) return fail("метод массива вызван на null");
            std::vector<Value>& elems = std::get<ObjArray*>(args[0])->elements;
            const long long n = static_cast<long long>(elems.size());
            switch (static_cast<Builtin>(id)) {
                case Builtin::ArrAppend:
                    elems.push_back(args[1]);
                    return doneNull();
                case Builtin::ArrPop: {
                    if (elems.empty()) return fail("pop() из пустого массива");
                    long long idx = n - 1;
                    if (args.size() == 2) {
                        idx = std::get<long long>(args[1]);
                        if (idx < 0) idx += n;
                        if (idx < 0 || idx >= n) {
                            return fail("pop(): индекс " + std::to_string(std::get<long long>(args[1])) +
                                        " вне границ массива (длина " + std::to_string(n) + ")");
                        }
                    }
                    Value out = elems[static_cast<size_t>(idx)];
                    elems.erase(elems.begin() + static_cast<std::ptrdiff_t>(idx));
                    return done(out);
                }
                case Builtin::ArrInsert: {
                    long long idx = std::get<long long>(args[1]);
                    if (idx < 0) {
                        idx += n;
                        if (idx < 0) idx = 0;
                    } else if (idx > n) {
                        idx = n;
                    }
                    elems.insert(elems.begin() + static_cast<std::ptrdiff_t>(idx), args[2]);
                    return doneNull();
                }
                case Builtin::ArrRemove: {
                    for (size_t i = 0; i < elems.size(); i++) {
                        if (valuesEqual(elems[i], args[1])) {
                            elems.erase(elems.begin() + static_cast<std::ptrdiff_t>(i));
                            return doneNull();
                        }
                    }
                    return fail("remove(): значение " + valueToDisplayString(args[1]) + " не найдено в массиве");
                }
                case Builtin::ArrContains:
                case Builtin::ArrIndexOf: {
                    long long found = -1;
                    for (size_t i = 0; i < elems.size(); i++) {
                        if (valuesEqual(elems[i], args[1])) {
                            found = static_cast<long long>(i);
                            break;
                        }
                    }
                    if (static_cast<Builtin>(id) == Builtin::ArrContains) return done(Value(found >= 0));
                    return done(integer(found));
                }
                case Builtin::ArrReverse:
                    std::reverse(elems.begin(), elems.end());
                    return doneNull();
                case Builtin::ArrClear:
                    elems.clear();
                    return doneNull();
                case Builtin::ArrSort: {
                    bool allNumbers = true, allStrings = true;
                    for (const Value& v : elems) {
                        allNumbers = allNumbers && isNumber(v);
                        allStrings = allStrings && isString(v);
                    }
                    if (!allNumbers && !allStrings) return fail("sort() умеет сортировать только числа или только строки");
                    if (allNumbers) {
                        std::stable_sort(elems.begin(), elems.end(), [](const Value& a, const Value& b) {
                            if (isInt(a) && isInt(b)) return std::get<long long>(a) < std::get<long long>(b);
                            return toDouble(a) < toDouble(b);
                        });
                    } else {
                        std::stable_sort(elems.begin(), elems.end(),
                                         [](const Value& a, const Value& b) { return strOf(a) < strOf(b); });
                    }
                    return doneNull();
                }
                case Builtin::ArrJoin: {
                    if (!isString(args[1])) return fail("join(): разделитель должен быть строкой");
                    std::string out;
                    for (size_t i = 0; i < elems.size(); i++) {
                        if (i > 0) out += strOf(args[1]);
                        out += valueToDisplayString(elems[i]);
                    }
                    return done(makeString(std::move(out)));
                }
                default:
                    break;
            }
            break;
        }

        // ------------------------------------------------------------------ методы строк
        case Builtin::StrUpper:
        case Builtin::StrLower:
        case Builtin::StrTrim:
        case Builtin::StrContains:
        case Builtin::StrStartsWith:
        case Builtin::StrEndsWith:
        case Builtin::StrIndexOf:
        case Builtin::StrReplace:
        case Builtin::StrSplit:
        case Builtin::StrRepeat: {
            if (!isString(args[0])) return fail("метод строки вызван на null");
            const std::string& s = strOf(args[0]);
            auto textArg = [&](size_t i) -> const std::string& { return strOf(args[i]); };
            switch (static_cast<Builtin>(id)) {
                case Builtin::StrUpper: return done(makeString(utf8::mapCase(s, true)));
                case Builtin::StrLower: return done(makeString(utf8::mapCase(s, false)));
                case Builtin::StrTrim: return done(makeString(trimAscii(s)));
                case Builtin::StrContains: return done(Value(s.find(textArg(1)) != std::string::npos));
                case Builtin::StrStartsWith: {
                    const std::string& p = textArg(1);
                    return done(Value(s.size() >= p.size() && s.compare(0, p.size(), p) == 0));
                }
                case Builtin::StrEndsWith: {
                    const std::string& p = textArg(1);
                    return done(Value(s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0));
                }
                case Builtin::StrIndexOf: {
                    size_t pos = s.find(textArg(1));
                    if (pos == std::string::npos) return done(integer(-1));
                    return done(integer(static_cast<long long>(utf8::charIndexOfByte(s, pos))));
                }
                case Builtin::StrReplace: {
                    const std::string& from = textArg(1);
                    const std::string& to = textArg(2);
                    if (from.empty()) return done(args[0]);
                    std::string out;
                    size_t pos = 0;
                    for (size_t hit; (hit = s.find(from, pos)) != std::string::npos; pos = hit + from.size()) {
                        out.append(s, pos, hit - pos);
                        out += to;
                    }
                    out.append(s, pos, std::string::npos);
                    return done(makeString(std::move(out)));
                }
                case Builtin::StrSplit: {
                    const std::string& sep = textArg(1);
                    std::vector<Value> parts;
                    if (sep.empty()) {
                        auto b = utf8::boundaries(s);
                        for (size_t i = 0; i + 1 < b.size(); i++) parts.push_back(makeString(s.substr(b[i], b[i + 1] - b[i])));
                    } else {
                        size_t pos = 0, hit;
                        while ((hit = s.find(sep, pos)) != std::string::npos) {
                            parts.push_back(makeString(s.substr(pos, hit - pos)));
                            pos = hit + sep.size();
                        }
                        parts.push_back(makeString(s.substr(pos)));
                    }
                    ObjArray* arr = allocateArray(std::move(parts));
                    push(Value(arr));
                    maybeCollectGarbage();
                    return true;
                }
                case Builtin::StrRepeat: {
                    long long times = std::get<long long>(args[1]);
                    if (times <= 0 || s.empty()) return done(makeString(""));
                    if (static_cast<size_t>(times) * s.size() > kMaxProducedSize) return fail("repeat(): результат слишком большой");
                    std::string out;
                    out.reserve(s.size() * static_cast<size_t>(times));
                    for (long long i = 0; i < times; i++) out += s;
                    return done(makeString(std::move(out)));
                }
                default:
                    break;
            }
            break;
        }
    }
    return fail("встроенная функция #" + std::to_string(id) + " не реализована в VM");
}

}  // namespace glass

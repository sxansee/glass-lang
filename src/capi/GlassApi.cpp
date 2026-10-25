#include "GlassApi.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

#include "../codegen/Compiler.h"
#include "../lexer/Lexer.h"
#include "../parser/Parser.h"
#include "../prelude/Prelude.h"
#include "../sema/BodyChecker.h"
#include "../sema/Sema.h"
#include "../vm/VM.h"

using namespace glass;

struct GlassSession {
    std::unique_ptr<VM> vm;
};

namespace {

// Единственное место аллокации строк, которые уходят через границу C —
// glass_free_string (тоже единственное место освобождения) обязано
// использовать ТОТ ЖЕ механизм (delete[]), не free()/CoTaskMemFree — в
// смешивании аллокаторов между модулями на Windows и кроется большая
// часть боли "работает в отладке, падает в релизе".
char* allocateCString(const std::string& s) {
    char* buf = new char[s.size() + 1];
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
}

void jsonEscapeInto(std::ostringstream& oss, const std::string& s) {
    oss << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    // Прочие управляющие байты (0x00-0x1F, кроме уже
                    // перечисленных) — \u00XX; печатное большинство (в
                    // т.ч. весь UTF-8 кириллицы, он же не ASCII control)
                    // проходит как есть — JSON-строки сами по себе UTF-8.
                    static const char* hex = "0123456789abcdef";
                    oss << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                } else {
                    oss << static_cast<char>(c);
                }
        }
    }
    oss << '"';
}

// Value -> JSON (см. GlassApi.h: форма у glass_get_locals_json). Примитивы
// — как есть; object/array — только ссылка по адресу, сами данные — в
// glass_get_heap_json под тем же адресом (иначе циклические ссылки в
// куче было бы не сериализовать, да и совпадающий объект дублировался
// бы на каждое поле/элемент, который на него указывает).
void valueToJsonInto(std::ostringstream& oss, const Value& v) {
    if (isInt(v)) {
        oss << std::get<long long>(v);
    } else if (isDouble(v)) {
        double d = std::get<double>(v);
        if (std::isfinite(d)) {
            // formatDouble — тот же формат, что у print: до 15 цифр и всегда с точкой или экспонентой
            // (3.0, а не 3 — иначе по JSON тип числа не отличить от int).
            oss << formatDouble(d);
        } else {
            // inf/nan — не JSON-числа (System.Text.Json на них падает), а
            // дебаггер не должен ломаться на программе, где 1e308 * 10.
            // Формат текста — тот же, что покажет print().
            std::ostringstream text;
            text << d;
            oss << "{\"kind\":\"double\",\"text\":";
            jsonEscapeInto(oss, text.str());
            oss << "}";
        }
    } else if (isBool(v)) {
        oss << (std::get<bool>(v) ? "true" : "false");
    } else if (isNull(v)) {
        oss << "null";
    } else if (isString(v)) {
        jsonEscapeInto(oss, *std::get<std::shared_ptr<std::string>>(v));
    } else if (isObject(v)) {
        ObjInstance* obj = std::get<ObjInstance*>(v);
        if (!obj) {
            oss << "null";
        } else {
            oss << "{\"ref\":";
            jsonEscapeInto(oss, std::to_string(reinterpret_cast<uintptr_t>(obj)));
            oss << ",\"kind\":\"object\",\"class\":";
            jsonEscapeInto(oss, obj->classInfo->name);
            oss << "}";
        }
    } else if (isArray(v)) {
        ObjArray* arr = std::get<ObjArray*>(v);
        if (!arr) {
            oss << "null";
        } else {
            oss << "{\"ref\":";
            jsonEscapeInto(oss, std::to_string(reinterpret_cast<uintptr_t>(arr)));
            oss << ",\"kind\":\"array\",\"length\":" << arr->elements.size() << "}";
        }
    } else {
        oss << "null";
    }
}

std::string localsToJson(const std::vector<VM::LocalVarView>& locals) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < locals.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"name\":";
        jsonEscapeInto(oss, locals[i].name);
        oss << ",\"value\":";
        valueToJsonInto(oss, locals[i].value);
        oss << "}";
    }
    oss << "]";
    return oss.str();
}

int toStatusCode(VM::RunStatus status) {
    switch (status) {
        case VM::RunStatus::Finished: return GLASS_STATUS_FINISHED;
        case VM::RunStatus::PausedAtBreakpoint: return GLASS_STATUS_PAUSED_AT_BREAKPOINT;
        case VM::RunStatus::PausedAfterStep: return GLASS_STATUS_PAUSED_AFTER_STEP;
        case VM::RunStatus::NotStarted: return GLASS_STATUS_FINISHED;  // не должно происходить после первого вызова
    }
    return GLASS_STATUS_FINISHED;
}

}  // namespace

extern "C" {

GLASS_API GlassSession* glass_compile(const char* source, char** error_out) {
    if (error_out) *error_out = nullptr;
    if (!source) {
        if (error_out) *error_out = allocateCString("glass_compile: source == NULL");
        return nullptr;
    }

    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    if (!parser.errors().empty()) {
        if (error_out) *error_out = allocateCString(parser.errors()[0]);
        return nullptr;
    }

    prependPrelude(program);

    Sema sema;
    auto declErrors = sema.analyze(program);
    if (!declErrors.empty()) {
        if (error_out) *error_out = allocateCString(declErrors[0]);
        return nullptr;
    }

    BodyChecker bodyChecker(sema);
    auto bodyErrors = bodyChecker.checkProgram(program);
    if (!bodyErrors.empty()) {
        if (error_out) *error_out = allocateCString(bodyErrors[0]);
        return nullptr;
    }

    Compiler compiler(sema);
    CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(program);
    } catch (const std::exception& e) {
        if (error_out) *error_out = allocateCString(e.what());
        return nullptr;
    }

    auto* session = new GlassSession();
    session->vm = std::make_unique<VM>(std::move(compiled));
    session->vm->setEchoOutput(false);   // вывод забирают через glass_get_output; в stdout хозяина он не нужен
    return session;
}

GLASS_API void glass_session_free(GlassSession* session) { delete session; }

GLASS_API void glass_free_string(char* s) { delete[] s; }

GLASS_API int glass_run(GlassSession* session) {
    if (!session || !session->vm) return 1;
    return session->vm->run();
}

GLASS_API char* glass_get_output(GlassSession* session) {
    if (!session || !session->vm) return allocateCString("");
    const auto& lines = session->vm->printedLines();
    std::ostringstream oss;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) oss << "\n";
        oss << lines[i];
    }
    return allocateCString(oss.str());
}

GLASS_API int glass_has_error(GlassSession* session) {
    if (!session || !session->vm) return 1;
    return session->vm->errors().empty() ? 0 : 1;
}

GLASS_API char* glass_get_error(GlassSession* session) {
    if (!session || !session->vm || session->vm->errors().empty()) return nullptr;
    return allocateCString(session->vm->errors()[0]);
}

GLASS_API void glass_set_breakpoint(GlassSession* session, int line) {
    if (session && session->vm) session->vm->setBreakpoint(line);
}
GLASS_API void glass_clear_breakpoint(GlassSession* session, int line) {
    if (session && session->vm) session->vm->clearBreakpoint(line);
}
GLASS_API void glass_clear_all_breakpoints(GlassSession* session) {
    if (session && session->vm) session->vm->clearAllBreakpoints();
}

GLASS_API int glass_run_until_pause(GlassSession* session) {
    if (!session || !session->vm) return GLASS_STATUS_FINISHED;
    return toStatusCode(session->vm->runUntilPause());
}
GLASS_API int glass_step_into(GlassSession* session) {
    if (!session || !session->vm) return GLASS_STATUS_FINISHED;
    return toStatusCode(session->vm->stepInto());
}
GLASS_API int glass_step_over(GlassSession* session) {
    if (!session || !session->vm) return GLASS_STATUS_FINISHED;
    return toStatusCode(session->vm->stepOver());
}
GLASS_API int glass_step_out(GlassSession* session) {
    if (!session || !session->vm) return GLASS_STATUS_FINISHED;
    return toStatusCode(session->vm->stepOut());
}

GLASS_API void glass_request_stop(GlassSession* session) {
    // Нарочно без блокировок: см. glass_request_stop в GlassApi.h — это
    // единственный вызов, допустимый из другого потока; внутри — атомарный флаг.
    if (session && session->vm) session->vm->requestStop();
}

GLASS_API int glass_current_line(GlassSession* session) {
    if (!session || !session->vm) return -1;
    return session->vm->currentLine();
}

GLASS_API char* glass_get_call_stack_json(GlassSession* session) {
    if (!session || !session->vm) return allocateCString("[]");
    std::ostringstream oss;
    oss << "[";
    auto stack = session->vm->callStack();
    for (size_t i = 0; i < stack.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"function\":";
        jsonEscapeInto(oss, stack[i].functionName);
        oss << ",\"line\":" << stack[i].line << "}";
    }
    oss << "]";
    return allocateCString(oss.str());
}

GLASS_API char* glass_get_locals_json(GlassSession* session, int frame_depth) {
    if (!session || !session->vm || frame_depth < 0) return allocateCString("[]");
    return allocateCString(localsToJson(session->vm->locals(static_cast<size_t>(frame_depth))));
}

GLASS_API char* glass_get_heap_json(GlassSession* session) {
    if (!session || !session->vm) return allocateCString("{\"objects\":[]}");
    std::ostringstream oss;
    oss << "{\"objects\":[";
    auto objects = session->vm->heapObjects();
    for (size_t i = 0; i < objects.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& obj = objects[i];
        oss << "{\"address\":";
        jsonEscapeInto(oss, std::to_string(obj.address));
        oss << ",\"reachable\":" << (obj.reachable ? "true" : "false");
        if (obj.isArray) {
            oss << ",\"kind\":\"array\",\"elements\":[";
            for (size_t j = 0; j < obj.fields.size(); ++j) {
                if (j > 0) oss << ",";
                valueToJsonInto(oss, obj.fields[j].value);
            }
            oss << "]}";
        } else {
            oss << ",\"kind\":\"object\",\"class\":";
            jsonEscapeInto(oss, obj.className);
            oss << ",\"fields\":" << localsToJson(obj.fields) << "}";
        }
    }
    oss << "]}";
    return allocateCString(oss.str());
}

GLASS_API int glass_collect_garbage(GlassSession* session) {
    if (!session || !session->vm) return 0;
    return static_cast<int>(session->vm->collectGarbageNow());
}

GLASS_API char* glass_get_object_info_json(GlassSession* session, const char* address) {
    if (!session || !session->vm || !address || *address == '\0') return allocateCString("null");

    // Только десятичные цифры, без знака/пробелов/хвоста — строгий разбор,
    // потому что строка пришла извне, а число в итоге сравнивается с
    // адресами реальных объектов.
    for (const char* p = address; *p; ++p) {
        if (*p < '0' || *p > '9') return allocateCString("null");
    }
    errno = 0;
    unsigned long long parsed = std::strtoull(address, nullptr, 10);
    if (errno == ERANGE) return allocateCString("null");

    VM::ObjectInspection info;
    if (!session->vm->inspectObject(static_cast<uintptr_t>(parsed), info)) return allocateCString("null");

    std::ostringstream oss;
    oss << "{\"address\":";
    jsonEscapeInto(oss, std::to_string(info.address));
    if (info.isArray) {
        oss << ",\"kind\":\"array\",\"length\":" << info.length << "}";
        return allocateCString(oss.str());
    }
    oss << ",\"kind\":\"object\",\"class\":";
    jsonEscapeInto(oss, info.className);

    oss << ",\"ancestry\":[";
    for (size_t i = 0; i < info.ancestry.size(); ++i) {
        if (i > 0) oss << ",";
        jsonEscapeInto(oss, info.ancestry[i]);
    }
    oss << "],\"layout\":[";
    for (size_t i = 0; i < info.layout.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& f = info.layout[i];
        oss << "{\"name\":";
        jsonEscapeInto(oss, f.name);
        oss << ",\"offset\":" << f.offset << ",\"size\":" << f.size << ",\"type\":";
        jsonEscapeInto(oss, f.type);
        oss << "}";
    }
    oss << "],\"vtable\":[";
    for (size_t i = 0; i < info.vtable.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& e = info.vtable[i];
        oss << "{\"method\":";
        jsonEscapeInto(oss, e.method);
        oss << ",\"implementedBy\":";
        jsonEscapeInto(oss, e.implementedBy);
        oss << "}";
    }
    oss << "]}";
    return allocateCString(oss.str());
}

}  // extern "C"

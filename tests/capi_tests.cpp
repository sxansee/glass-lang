#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "../src/capi/GlassApi.h"
#include "test_framework.h"

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// --- Компиляция ---

void test_compile_valid_source_succeeds() {
    char* error = nullptr;
    GlassSession* s = glass_compile("fn main() { print(1); }\n", &error);
    CHECK(s != nullptr);
    CHECK(error == nullptr);
    if (error) glass_free_string(error);
    if (s) glass_session_free(s);
}

void test_compile_invalid_source_fails_with_error() {
    char* error = nullptr;
    GlassSession* s = glass_compile("fn main( {\n", &error);  // сломанный синтаксис
    CHECK(s == nullptr);
    CHECK(error != nullptr);
    if (error) glass_free_string(error);
    if (s) glass_session_free(s);
}

void test_compile_null_source_is_safe() {
    char* error = nullptr;
    GlassSession* s = glass_compile(nullptr, &error);
    CHECK(s == nullptr);
    CHECK(error != nullptr);
    if (error) glass_free_string(error);
}

// --- Запуск целиком ---

void test_run_and_get_output() {
    GlassSession* s = glass_compile("fn main() { print(1); print(2); }\n", nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    int code = glass_run(s);
    CHECK(code == 0);
    char* output = glass_get_output(s);
    CHECK(std::string(output) == "1\n2");
    glass_free_string(output);
    CHECK(glass_has_error(s) == 0);
    glass_session_free(s);
}

// Сессия C API не дублирует print() в stdout процесса-хозяина: вывод забирают через glass_get_output.
void test_run_does_not_echo_to_stdout() {
    std::ostringstream captured;
    std::streambuf* old = std::cout.rdbuf(captured.rdbuf());
    GlassSession* s = glass_compile("fn main() { print(\"тихо\"); }\n", nullptr);
    CHECK(s != nullptr);
    if (s) glass_run(s);
    std::cout.rdbuf(old);
    CHECK_EQ(captured.str(), std::string(""));
    if (s) {
        char* output = glass_get_output(s);
        CHECK_EQ(std::string(output), std::string("тихо"));
        glass_free_string(output);
        glass_session_free(s);
    }
}

void test_runtime_error_is_reported() {
    GlassSession* s = glass_compile("fn main() { let a = [1, 2]; print(a[5]); }\n", nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    int code = glass_run(s);
    CHECK(code != 0);
    CHECK(glass_has_error(s) == 1);
    char* error = glass_get_error(s);
    CHECK(error != nullptr);
    if (error) {
        CHECK(contains(error, "вне границ"));
        glass_free_string(error);
    }
    glass_session_free(s);
}

// --- Точки останова / шаги ---

void test_breakpoint_pauses_and_current_line_matches() {
    std::string src =
        "fn main() {\n"
        "    print(1);\n"
        "    print(2);\n"
        "}\n";
    GlassSession* s = glass_compile(src.c_str(), nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 3);
    int status = glass_run_until_pause(s);
    CHECK(status == GLASS_STATUS_PAUSED_AT_BREAKPOINT);
    CHECK(glass_current_line(s) == 3);
    status = glass_run_until_pause(s);
    CHECK(status == GLASS_STATUS_FINISHED);
    glass_session_free(s);
}

void test_step_status_codes() {
    GlassSession* s = glass_compile("fn main() { print(1); }\n", nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    int status = glass_step_into(s);  // первый шаг — просто встать на вход
    CHECK(status == GLASS_STATUS_PAUSED_AFTER_STEP);
    status = glass_step_into(s);  // выполнить print(1), дойти до конца функции
    CHECK(status == GLASS_STATUS_FINISHED || status == GLASS_STATUS_PAUSED_AFTER_STEP);
    glass_session_free(s);
}

// --- JSON: стек вызовов / локальные переменные / куча ---

void test_call_stack_json_shape() {
    std::string src =
        "fn helper() -> int {\n"
        "    return 1;\n"
        "}\n"
        "fn main() {\n"
        "    let x = helper();\n"
        "    print(x);\n"
        "}\n";
    GlassSession* s = glass_compile(src.c_str(), nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 2);
    glass_run_until_pause(s);
    char* json = glass_get_call_stack_json(s);
    CHECK(json != nullptr);
    if (json) {
        std::string j(json);
        CHECK(contains(j, "\"function\":\"helper\""));
        CHECK(contains(j, "\"function\":\"main\""));
        CHECK(contains(j, "\"line\":2"));
        glass_free_string(json);
    }
    glass_session_free(s);
}

void test_locals_json_shape() {
    std::string src =
        "fn main() {\n"
        "    let a = 10;\n"
        "    print(a);\n"
        "}\n";
    GlassSession* s = glass_compile(src.c_str(), nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 3);
    glass_run_until_pause(s);
    char* json = glass_get_locals_json(s, 0);
    CHECK(json != nullptr);
    if (json) {
        std::string j(json);
        CHECK(contains(j, "\"name\":\"a\""));
        CHECK(contains(j, "\"value\":10"));
        glass_free_string(json);
    }
    glass_session_free(s);
}

void test_heap_json_matches_local_reference() {
    std::string src =
        "mold Point {\n"
        "    let x: int\n"
        "    Point(v: int) { x = v; }\n"
        "}\n"
        "fn main() {\n"
        "    let p = Point(7);\n"
        "    print(p.x);\n"
        "}\n";
    GlassSession* s = glass_compile(src.c_str(), nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 7);
    glass_run_until_pause(s);

    char* localsJson = glass_get_locals_json(s, 0);
    char* heapJson = glass_get_heap_json(s);
    CHECK(localsJson != nullptr);
    CHECK(heapJson != nullptr);
    if (localsJson && heapJson) {
        std::string locals(localsJson);
        std::string heap(heapJson);
        CHECK(contains(locals, "\"kind\":\"object\""));
        CHECK(contains(locals, "\"class\":\"Point\""));
        // Достаём "ref":"<адрес>" из locals и проверяем, что то же
        // "address":"<адрес>" встречается в heap — локальная переменная и
        // объект в куче должны ссылаться друг на друга одним и тем же
        // адресом (см. GlassApi.h: graph по ссылкам, не разворачивание вглубь).
        size_t refPos = locals.find("\"ref\":\"");
        CHECK(refPos != std::string::npos);
        if (refPos != std::string::npos) {
            size_t start = refPos + 7;
            size_t end = locals.find('"', start);
            std::string address = locals.substr(start, end - start);
            CHECK(contains(heap, "\"address\":\"" + address + "\""));
            CHECK(contains(heap, "\"x\""));
        }
    }
    if (localsJson) glass_free_string(localsJson);
    if (heapJson) glass_free_string(heapJson);
    glass_session_free(s);
}

// --- Безопасность при неверном использовании ---

void test_null_session_does_not_crash() {
    CHECK(glass_run(nullptr) != 0);
    CHECK(glass_has_error(nullptr) == 1);
    CHECK(glass_get_error(nullptr) == nullptr);
    CHECK(glass_current_line(nullptr) == -1);
    glass_set_breakpoint(nullptr, 1);       // не должно упасть
    glass_clear_breakpoint(nullptr, 1);     // не должно упасть
    glass_clear_all_breakpoints(nullptr);   // не должно упасть
    char* output = glass_get_output(nullptr);
    CHECK(output != nullptr);
    if (output) glass_free_string(output);
    char* stack = glass_get_call_stack_json(nullptr);
    CHECK(stack != nullptr);
    if (stack) glass_free_string(stack);
    glass_session_free(nullptr);  // delete nullptr — легален и безопасен
}

// --- Расширения для IDE: остановка, ручной GC, инспектор объекта, inf/nan ---

std::string takeString(char* p) {
    std::string s = p ? p : "";
    if (p) glass_free_string(p);
    return s;
}

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + needle.size())) {
        count++;
    }
    return count;
}

// Первый "ref":"<адрес>" из JSON локальных переменных/кучи.
std::string firstRef(const std::string& json) {
    size_t pos = json.find("\"ref\":\"");
    if (pos == std::string::npos) return "";
    size_t start = pos + 7;
    size_t end = json.find('"', start);
    return json.substr(start, end - start);
}

void test_request_stop_interrupts_infinite_loop() {
    GlassSession* s = glass_compile(
        "fn main() {\n"
        "    let i = 0;\n"
        "    while true {\n"
        "        i += 1;\n"
        "    }\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;

    std::atomic<bool> done{false};
    std::atomic<int> code{-1};
    std::thread worker([&] {
        code = glass_run(s);
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!done.load());  // программа действительно крутится, а не закончилась сама
    glass_request_stop(s);

    // Если остановка не сработает — тест не должен вешать сборку навсегда.
    for (int i = 0; i < 500 && !done.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(done.load());
    if (!done.load()) {
        std::cerr << "request_stop не остановил цикл — выходим принудительно\n";
        std::_Exit(2);
    }
    worker.join();
    CHECK_EQ(code.load(), 1);
    CHECK(glass_has_error(s) == 1);
    CHECK(contains(takeString(glass_get_error(s)), "прерван"));
    glass_session_free(s);
}

void test_request_stop_before_run_and_during_pause() {
    // До запуска: первая же инструкция завершает выполнение.
    GlassSession* s = glass_compile("fn main() { print(1); }\n", nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_request_stop(s);
    CHECK_EQ(glass_run(s), 1);
    CHECK(contains(takeString(glass_get_error(s)), "прерван"));
    glass_session_free(s);

    // На паузе отладчика: следующий шаг уже не выполняется.
    GlassSession* d = glass_compile(
        "fn main() {\n"
        "    let a = 1;\n"
        "    print(a);\n"
        "    print(a);\n"
        "}\n",
        nullptr);
    CHECK(d != nullptr);
    if (!d) return;
    glass_set_breakpoint(d, 3);
    CHECK_EQ(glass_run_until_pause(d), GLASS_STATUS_PAUSED_AT_BREAKPOINT);
    glass_request_stop(d);
    CHECK_EQ(glass_step_over(d), GLASS_STATUS_FINISHED);
    CHECK(glass_has_error(d) == 1);
    CHECK_EQ(takeString(glass_get_output(d)), std::string(""));  // print(a) на строке 3 не выполнился
    glass_session_free(d);
}

void test_collect_garbage_frees_unreachable_objects() {
    GlassSession* s = glass_compile(
        "mold Box {\n"
        "    let v: int\n"
        "    Box(x: int) { v = x; }\n"
        "}\n"
        "fn main() {\n"
        "    let keep = Box(1);\n"
        "    let tmp = Box(2);\n"
        "    tmp = keep;\n"
        "    print(keep.v);\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 9);
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_PAUSED_AT_BREAKPOINT);

    // Box(2) уже недостижим (tmp переназначен), но ещё не собран — он виден в куче,
    // и снимок честно помечает его "reachable":false (ровно то, что освободит GC).
    std::string before = takeString(glass_get_heap_json(s));
    CHECK_EQ(countOccurrences(before, "\"address\":"), static_cast<size_t>(2));
    CHECK_EQ(countOccurrences(before, "\"reachable\":false"), static_cast<size_t>(1));
    CHECK_EQ(countOccurrences(before, "\"reachable\":true"), static_cast<size_t>(1));
    CHECK_EQ(glass_collect_garbage(s), 1);
    std::string after = takeString(glass_get_heap_json(s));
    CHECK_EQ(countOccurrences(after, "\"address\":"), static_cast<size_t>(1));
    CHECK_EQ(countOccurrences(after, "\"reachable\":false"), static_cast<size_t>(0));
    CHECK_EQ(glass_collect_garbage(s), 0);  // повторный сбор — нечего собирать

    // Живой объект цел, программа дорабатывает как ни в чём не бывало.
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_FINISHED);
    CHECK_EQ(takeString(glass_get_output(s)), std::string("1"));
    CHECK(glass_has_error(s) == 0);
    glass_session_free(s);
}

void test_object_info_json_for_object() {
    GlassSession* s = glass_compile(
        "mold Animal {\n"
        "    let name: string\n"
        "    let age: int\n"
        "    Animal(n: string, a: int) { name = n; age = a; }\n"
        "    virtual fn speak() -> string { return \"...\"; }\n"
        "}\n"
        "mold Dog : Animal {\n"
        "    let breed: string\n"
        "    Dog(n: string, a: int, b: string) { super(n, a); breed = b; }\n"
        "    override fn speak() -> string { return \"Woof\"; }\n"
        "}\n"
        "fn main() {\n"
        "    let d = Dog(\"Rex\", 3, \"Lab\");\n"
        "    print(d~layout);\n"
        "    print(d~vtable);\n"
        "    print(1);\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 16);
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_PAUSED_AT_BREAKPOINT);

    std::string ref = firstRef(takeString(glass_get_locals_json(s, 0)));
    CHECK(!ref.empty());
    std::string info = takeString(glass_get_object_info_json(s, ref.c_str()));
    CHECK(contains(info, "\"address\":\"" + ref + "\""));
    CHECK(contains(info, "\"kind\":\"object\""));
    CHECK(contains(info, "\"class\":\"Dog\""));
    CHECK(contains(info, "\"ancestry\":[\"Dog\",\"Animal\"]"));
    // Унаследованные поля первыми, потом свои — тот же порядок, что у ~layout.
    CHECK(contains(info, "{\"name\":\"name\",\"offset\":0,\"size\":8,\"type\":\"ref\"}"));
    CHECK(contains(info, "{\"name\":\"age\",\"offset\":1,\"size\":8,\"type\":\"int\"}"));
    CHECK(contains(info, "{\"name\":\"breed\",\"offset\":2,\"size\":8,\"type\":\"ref\"}"));
    // override: speak реализует Dog, не Animal.
    CHECK(contains(info, "{\"method\":\"speak\",\"implementedBy\":\"Dog\"}"));

    // Инспектор и сам язык показывают одно и то же (общий layoutOf/vtableOf).
    std::string output = takeString(glass_get_output(s));
    CHECK(contains(output, "name@0 (8 байт)"));
    CHECK(contains(output, "age@1 (8 байт)"));
    CHECK(contains(output, "breed@2 (8 байт)"));
    CHECK(contains(output, "speak -> Dog.speak"));
    glass_session_free(s);
}

void test_object_info_json_for_array_and_invalid_addresses() {
    GlassSession* s = glass_compile(
        "fn main() {\n"
        "    let xs = [1, 2, 3];\n"
        "    print(xs[0]);\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 3);
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_PAUSED_AT_BREAKPOINT);

    std::string ref = firstRef(takeString(glass_get_locals_json(s, 0)));
    CHECK(!ref.empty());
    std::string info = takeString(glass_get_object_info_json(s, ref.c_str()));
    CHECK(contains(info, "\"kind\":\"array\""));
    CHECK(contains(info, "\"length\":3"));

    // Любая не-адрес строка — null, без чтения памяти по такому "адресу".
    const char* bad[] = {"", "abc", "-5", "12x", " 12", "0", "4096", "99999999999999999999999999"};
    for (const char* b : bad) {
        CHECK_EQ(takeString(glass_get_object_info_json(s, b)), std::string("null"));
    }
    CHECK_EQ(takeString(glass_get_object_info_json(s, nullptr)), std::string("null"));
    CHECK_EQ(takeString(glass_get_object_info_json(nullptr, ref.c_str())), std::string("null"));
    glass_session_free(s);
}

void test_non_finite_double_is_valid_json() {
    GlassSession* s = glass_compile(
        "fn main() {\n"
        "    let x = 10.0;\n"
        "    let i = 0;\n"
        "    while i < 400 {\n"
        "        x = x * 10.0;\n"
        "        i += 1;\n"
        "    }\n"
        "    print(i);\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 8);
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_PAUSED_AT_BREAKPOINT);
    std::string locals = takeString(glass_get_locals_json(s, 0));
    // Не голый inf (невалидный JSON), а объект с текстом.
    CHECK(contains(locals, "\"name\":\"x\",\"value\":{\"kind\":\"double\",\"text\":\"inf\"}"));
    CHECK(!contains(locals, ":inf"));
    glass_session_free(s);
}

void test_prelude_code_is_invisible_to_debugger() {
    // Строки пролога (mold Exception и др.) не должны путаться со строками
    // пользователя: ctor Exception исполняется на "своей" строке 5, и точка
    // останова на ЧУЖОЙ строке 5, которая никогда не выполняется, не должна
    // срабатывать внутри служебного кода.
    const char* src =
        "fn main() {\n"                          // 1
        "    let x = 0;\n"                       // 2
        "    if x == 1 {\n"                      // 3
        "        print(\"never\");\n"            // 4
        "        print(\"never 2\");\n"          // 5  <- точка останова, строка не выполняется
        "    }\n"                                // 6
        "    let ok = Exception(\"boom\");\n"    // 7
        "    print(ok.message);\n"               // 8
        "}\n";
    GlassSession* s = glass_compile(src, nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 5);
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_FINISHED);
    CHECK_EQ(takeString(glass_get_output(s)), std::string("boom"));
    glass_session_free(s);

    // stepInto по строке с конструктором из пролога не заходит внутрь него:
    // следующая остановка — на следующей СТРОКЕ ПОЛЬЗОВАТЕЛЯ, стек — из одного фрейма.
    GlassSession* d = glass_compile(src, nullptr);
    CHECK(d != nullptr);
    if (!d) return;
    glass_set_breakpoint(d, 7);
    CHECK_EQ(glass_run_until_pause(d), GLASS_STATUS_PAUSED_AT_BREAKPOINT);
    CHECK_EQ(glass_current_line(d), 7);
    CHECK_EQ(glass_step_into(d), GLASS_STATUS_PAUSED_AFTER_STEP);
    CHECK_EQ(glass_current_line(d), 8);
    std::string stack = takeString(glass_get_call_stack_json(d));
    CHECK_EQ(countOccurrences(stack, "\"function\""), static_cast<size_t>(1));
    CHECK(!contains(stack, "Exception"));
    glass_session_free(d);
}

void test_infinite_recursion_is_a_runtime_error_not_a_crash() {
    // Раньше глубина вызовов не ограничивалась: бесконечная рекурсия росла, пока не
    // кончалась память — и роняла весь процесс (в IDE — всю среду разработки).
    GlassSession* s = glass_compile(
        "fn f(n: int) -> int {\n"
        "    return f(n + 1);\n"
        "}\n"
        "fn main() {\n"
        "    print(f(0));\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK_EQ(glass_run(s), 1);
    CHECK(contains(takeString(glass_get_error(s)), "переполнение стека вызовов"));
    glass_session_free(s);

    // Рекурсия умеренной глубины работает как раньше.
    GlassSession* ok = glass_compile(
        "fn sum(n: int) -> int {\n"
        "    if n == 0 { return 0; }\n"
        "    return n + sum(n - 1);\n"
        "}\n"
        "fn main() {\n"
        "    print(sum(5000));\n"
        "}\n",
        nullptr);
    CHECK(ok != nullptr);
    if (!ok) return;
    CHECK_EQ(glass_run(ok), 0);
    CHECK_EQ(takeString(glass_get_output(ok)), std::string("12502500"));
    glass_session_free(ok);
}

void test_gc_survives_very_long_linked_list() {
    // Обход при сборке мусора раньше был рекурсивным: список в сотни тысяч узлов
    // переполнял стек C++ (на Windows — уже на десятках тысяч, особенно на
    // потоке пула, где работает IDE). Теперь обход итеративный.
    GlassSession* s = glass_compile(
        "mold Node {\n"
        "    let value: int\n"
        "    let next: Node\n"
        "    Node(v: int, n: Node) { value = v; next = n; }\n"
        "}\n"
        "fn main() {\n"
        "    let head: Node = null;\n"
        "    let i = 0;\n"
        "    while i < 300000 {\n"
        "        head = Node(i, head);\n"
        "        i += 1;\n"
        "    }\n"
        "    ~gc\n"
        "    print(head.value);\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK_EQ(glass_run(s), 0);
    CHECK_EQ(takeString(glass_get_output(s)), std::string("299999"));
    glass_session_free(s);
}

void test_double_json_is_distinguishable_from_int() {
    GlassSession* s = glass_compile(
        "fn main() {\n"
        "    let whole = 3.0;\n"
        "    let frac = 2.5;\n"
        "    let n = 3;\n"
        "    print(n);\n"
        "}\n",
        nullptr);
    CHECK(s != nullptr);
    if (!s) return;
    glass_set_breakpoint(s, 5);
    CHECK_EQ(glass_run_until_pause(s), GLASS_STATUS_PAUSED_AT_BREAKPOINT);
    std::string locals = takeString(glass_get_locals_json(s, 0));
    CHECK(contains(locals, "{\"name\":\"whole\",\"value\":3.0}"));   // не 3
    CHECK(contains(locals, "{\"name\":\"frac\",\"value\":2.5}"));
    CHECK(contains(locals, "{\"name\":\"n\",\"value\":3}"));        // int остаётся int
    glass_session_free(s);
}

void test_new_functions_null_safe() {
    glass_request_stop(nullptr);                 // не должно упасть
    CHECK_EQ(glass_collect_garbage(nullptr), 0);
    CHECK_EQ(takeString(glass_get_object_info_json(nullptr, nullptr)), std::string("null"));
}

} // namespace

int main() {
    test_compile_valid_source_succeeds();
    test_compile_invalid_source_fails_with_error();
    test_compile_null_source_is_safe();

    test_run_and_get_output();
    test_run_does_not_echo_to_stdout();
    test_runtime_error_is_reported();

    test_breakpoint_pauses_and_current_line_matches();
    test_step_status_codes();

    test_call_stack_json_shape();
    test_locals_json_shape();
    test_heap_json_matches_local_reference();

    test_null_session_does_not_crash();

    test_request_stop_interrupts_infinite_loop();
    test_request_stop_before_run_and_during_pause();
    test_collect_garbage_frees_unreachable_objects();
    test_object_info_json_for_object();
    test_object_info_json_for_array_and_invalid_addresses();
    test_non_finite_double_is_valid_json();
    test_prelude_code_is_invisible_to_debugger();
    test_infinite_recursion_is_a_runtime_error_not_a_crash();
    test_gc_survives_very_long_linked_list();
    test_double_json_is_distinguishable_from_int();
    test_new_functions_null_safe();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

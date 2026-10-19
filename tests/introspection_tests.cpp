#include <iostream>
#include <string>

#include "../src/codegen/Compiler.h"
#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "../src/prelude/Prelude.h"
#include "../src/sema/BodyChecker.h"
#include "../src/sema/Sema.h"
#include "../src/vm/VM.h"
#include "test_framework.h"

using namespace glass;

namespace {

struct RunResult {
    std::vector<std::string> output;
    size_t liveObjectsAfterRun = 0;
    bool ok = true;
};

RunResult runSource(const std::string& src) {
    RunResult result;
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        result.ok = false;
        result.output = {"ERROR: parse: " + parser.errors()[0]};
        return result;
    }

    prependPrelude(prog);

    Sema sema;
    auto declErrs = sema.analyze(prog);
    if (!declErrs.empty()) {
        result.ok = false;
        result.output = {"ERROR: sema: " + declErrs[0]};
        return result;
    }

    BodyChecker checker(sema);
    auto bodyErrs = checker.checkProgram(prog);
    if (!bodyErrs.empty()) {
        result.ok = false;
        result.output = {"ERROR: types: " + bodyErrs[0]};
        return result;
    }

    Compiler compiler(sema);
    CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(prog);
    } catch (const std::exception& e) {
        result.ok = false;
        result.output = {std::string("ERROR: codegen: ") + e.what()};
        return result;
    }

    VM vm(std::move(compiled));
    vm.run();
    if (!vm.errors().empty()) {
        result.ok = false;
        result.output = {"ERROR: runtime: " + vm.errors()[0]};
        return result;
    }
    result.output = vm.printedLines();
    result.liveObjectsAfterRun = vm.liveObjectCount();
    return result;
}

void expectLineContains(const std::string& name, const std::string& src, size_t lineIndex,
                         const std::string& needle) {
    auto result = runSource(src);
    bool found = lineIndex < result.output.size() && result.output[lineIndex].find(needle) != std::string::npos;
    if (!found) {
        std::cerr << "FAILED: " << name << " ожидал строку " << lineIndex << " содержащую '" << needle
                   << "', получил:";
        for (auto& l : result.output) std::cerr << " [" << l << "]";
        std::cerr << "\n";
    }
    CHECK(found);
}

// --- ~address ---

void test_address_is_nonzero() {
    std::string src = "mold Point { let x: int\n Point(v: int) { x = v; } }\nfn main() { print(Point(1)~address); }\n";
    auto result = runSource(src);
    CHECK(result.ok);
    bool nonZero = !result.output.empty() && result.output[0] != "0";
    CHECK(nonZero);
}

void test_address_differs_between_objects() {
    std::string src =
        "mold Point { let x: int\n Point(v: int) { x = v; } }\n"
        "fn main() {\n"
        "    let a = Point(1);\n"
        "    let b = Point(2);\n"
        "    print(a~address == b~address);\n"
        "}\n";
    expectLineContains("address_differs_between_objects", src, 0, "false");
}

// --- ~layout ---

void test_layout_lists_own_fields() {
    std::string src =
        "mold Point {\n"
        "    let x: int\n"
        "    let y: int\n"
        "    Point(a: int, b: int) { x = a; y = b; }\n"
        "}\n"
        "fn main() { print(Point(1, 2)~layout); }\n";
    expectLineContains("layout_lists_own_fields", src, 0, "x@0");
    expectLineContains("layout_lists_own_fields", src, 0, "y@1");
}

void test_layout_includes_inherited_fields_flattened() {
    std::string src =
        "mold Animal {\n"
        "    let name: string\n"
        "    Animal(n: string) { name = n; }\n"
        "}\n"
        "mold Dog : Animal {\n"
        "    let breed: string\n"
        "    Dog(n: string, b: string) { super(n); breed = b; }\n"
        "}\n"
        "fn main() { print(Dog(\"Rex\", \"Lab\")~layout); }\n";
    // Базовые поля идут первыми (та же раскладка, что для обычных
    // виртуальных вызовов, см. ClassInfo.h) — name@0 от Animal, breed@1 —
    // уже от Dog.
    expectLineContains("layout_includes_inherited_fields_flattened", src, 0, "name@0");
    expectLineContains("layout_includes_inherited_fields_flattened", src, 0, "breed@1");
}

// --- ~vtable ---

void test_vtable_shows_virtual_method_and_owner() {
    std::string src =
        "mold Animal {\n"
        "    Animal() { }\n"
        "    virtual fn speak() -> string { return \"...\"; }\n"
        "}\n"
        "fn main() { print(Animal()~vtable); }\n";
    expectLineContains("vtable_shows_virtual_method_and_owner", src, 0, "speak -> Animal.speak");
}

void test_vtable_shows_override_owner_not_base() {
    std::string src =
        "mold Animal {\n"
        "    Animal() { }\n"
        "    virtual fn speak() -> string { return \"...\"; }\n"
        "}\n"
        "mold Dog : Animal {\n"
        "    Dog() { super(); }\n"
        "    override fn speak() -> string { return \"Woof\"; }\n"
        "}\n"
        "fn main() { print(Dog()~vtable); }\n";
    expectLineContains("vtable_shows_override_owner_not_base", src, 0, "speak -> Dog.speak");
}

void test_vtable_via_facet_implementation() {
    std::string src =
        "facet Shape { fn area() -> double; }\n"
        "mold Square : Shape {\n"
        "    let side: double\n"
        "    Square(s: double) { side = s; }\n"
        "    fn area() -> double { return side * side; }\n"
        "}\n"
        "fn main() { print(Square(2.0)~vtable); }\n";
    expectLineContains("vtable_via_facet_implementation", src, 0, "area -> Square.area");
}

void test_vtable_empty_for_mold_without_virtual_methods() {
    std::string src = "mold Plain { let x: int\n Plain() { x = 0; } }\nfn main() { print(Plain()~vtable); }\n";
    expectLineContains("vtable_empty_for_mold_without_virtual_methods", src, 0, "vtable(Plain):");
}

// --- ~heap / ~gc ---

void test_heap_counts_live_objects() {
    std::string src =
        "mold Point { let x: int\n Point(v: int) { x = v; } }\n"
        "fn main() {\n"
        "    let a = Point(1);\n"
        "    let b = Point(2);\n"
        "    print(~heap);\n"
        "}\n";
    // 2 реальных объекта — ровно они и должны попасть в снимок (снимок
    // строится до собственных объектов ~heap, см. VM.cpp OpCode::Heap).
    expectLineContains("heap_counts_live_objects", src, 0, "heap(2 объект(ов))");
}

void test_gc_after_manual_trigger_reclaims_unreachable() {
    // Цикл — в отдельной функции: она должна ПОЛНОСТЬЮ вернуться (весь её
    // фрейм со всеми слотами уходит со стека), прежде чем проверять кучу
    // в main. Внутри той же функции последняя переменная цикла (при
    // плоском распределении слотов без переиспользования — см. README
    // «Сознательные упрощения») физически задержалась бы в своём слоте
    // до конца функции и всё ещё считалась бы корнем — это не баг, а
    // задокументированное поведение, просто не то, что этот тест хочет
    // проверить.
    std::string src =
        "mold Point { let x: int\n Point(v: int) { x = v; } }\n"
        "fn makeGarbage() {\n"
        "    let i = 0;\n"
        "    while i < 50 {\n"
        "        let throwaway = Point(i);\n"
        "        i += 1;\n"
        "    }\n"
        "}\n"
        "fn main() {\n"
        "    makeGarbage();\n"
        "    ~gc;\n"
        "    print(~heap);\n"
        "}\n";
    expectLineContains("gc_after_manual_trigger_reclaims_unreachable", src, 0, "heap(0 объект(ов))");
}

void test_introspection_byproducts_are_ordinary_gc_objects() {
    // ~layout/~vtable сами выделяют LayoutInfo/VTableInfo и массивы под их
    // поля (см. VM.cpp) — если их не сохранить, они не должны утекать: та
    // же куча, тот же GC, что и у любого объекта программы.
    std::string src =
        "mold Point { let x: int\n Point(v: int) { x = v; } }\n"
        "fn main() {\n"
        "    let i = 0;\n"
        "    while i < 50 {\n"
        "        print(Point(i)~layout);\n"
        "        i += 1;\n"
        "    }\n"
        "    ~gc;\n"
        "    print(~heap);\n"
        "}\n";
    auto result = runSource(src);
    CHECK(result.ok);
    bool lastIsSmallHeap =
        !result.output.empty() && result.output.back().rfind("heap(0 объект(ов))", 0) == 0;
    if (!lastIsSmallHeap) {
        std::cerr << "FAILED: introspection_byproducts_are_ordinary_gc_objects — последняя строка: "
                   << (result.output.empty() ? "<нет вывода>" : result.output.back()) << "\n";
    }
    CHECK(lastIsSmallHeap);
}

// --- Вместе с массивами/исключениями (нет отдельного спецкейса для введённых ранее фич) ---

void test_layout_works_on_exception_instance() {
    // Exception — обычный mold из prelude (см. prelude/Prelude.h), а не
    // спецкейс — ~layout должен видеть его поле 'message' точно так же,
    // как поле любого пользовательского mold'а.
    std::string src = "fn main() { print(Exception(\"x\")~layout); }\n";
    expectLineContains("layout_works_on_exception_instance", src, 0, "message@0");
}

} // namespace

int main() {
    test_address_is_nonzero();
    test_address_differs_between_objects();

    test_layout_lists_own_fields();
    test_layout_includes_inherited_fields_flattened();

    test_vtable_shows_virtual_method_and_owner();
    test_vtable_shows_override_owner_not_base();
    test_vtable_via_facet_implementation();
    test_vtable_empty_for_mold_without_virtual_methods();

    test_heap_counts_live_objects();
    test_gc_after_manual_trigger_reclaims_unreachable();
    test_introspection_byproducts_are_ordinary_gc_objects();

    test_layout_works_on_exception_instance();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

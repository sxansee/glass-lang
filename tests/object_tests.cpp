#include <iostream>
#include <string>

#include "../src/codegen/Compiler.h"
#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
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

void expectOutput(const std::string& name, const std::string& src, const std::vector<std::string>& expected) {
    auto result = runSource(src);
    if (result.output != expected) {
        std::cerr << "FAILED: " << name << "\n  ожидалось:";
        for (auto& l : expected) std::cerr << " [" << l << "]";
        std::cerr << "\n  получено: ";
        for (auto& l : result.output) std::cerr << " [" << l << "]";
        std::cerr << "\n";
    }
    CHECK(result.output == expected);
}

// --- Конструкция и поля ---

void test_construction_and_field_read() {
    std::string src =
        "mold Circle {\n"
        "    let radius: double\n"
        "    Circle(r: double) { radius = r; }\n"
        "}\n"
        "fn main() { let c = Circle(3.0); print(c.radius); }";
    expectOutput("construction_and_field_read", src, {"3.0"});   // double печатается с точкой
}

void test_field_write_and_compound_assignment() {
    std::string src =
        "mold Counter {\n"
        "    let value: int\n"
        "    Counter() { value = 0; }\n"
        "    fn increment() { value += 1; }\n"
        "}\n"
        "fn main() { let c = Counter(); c.increment(); c.increment(); c.increment(); print(c.value); }";
    expectOutput("field_write_and_compound_assignment", src, {"3"});
}

void test_field_default_values() {
    std::string src =
        "mold Empty {\n"
        "    let n: int\n"
        "    let d: double\n"
        "    let b: bool\n"
        "    Empty() { }\n"
        "}\n"
        "fn main() { let e = Empty(); print(e.n); print(e.d); print(e.b); }";
    expectOutput("field_default_values", src, {"0", "0.0", "false"});
}

void test_field_initializer_runs() {
    std::string src =
        "mold Point {\n"
        "    let x: int = 10\n"
        "    Point() { }\n"
        "}\n"
        "fn main() { let p = Point(); print(p.x); }";
    expectOutput("field_initializer_runs", src, {"10"});
}

// --- Методы: не-виртуальные и виртуальные ---

void test_non_virtual_method_call() {
    std::string src =
        "mold Greeter {\n"
        "    fn greet() -> string { return \"hi\"; }\n"
        "}\n"
        "fn main() { let g = Greeter(); print(g.greet()); }";
    expectOutput("non_virtual_method_call", src, {"hi"});
}

void test_virtual_dispatch_through_facet() {
    // Сердце всей идеи языка: один и тот же вызов s.area() через Shape
    // должен вызывать РАЗНУЮ реализацию в зависимости от реального класса.
    std::string src =
        "facet Shape {\n"
        "    fn area() -> double;\n"
        "}\n"
        "mold Circle : Shape {\n"
        "    let radius: double\n"
        "    Circle(r: double) { radius = r; }\n"
        "    fn area() -> double { return radius * radius; }\n"
        "}\n"
        "mold Square : Shape {\n"
        "    let side: double\n"
        "    Square(s: double) { side = s; }\n"
        "    fn area() -> double { return side * side; }\n"
        "}\n"
        "fn printArea(s: Shape) { print(s.area()); }\n"
        "fn main() { printArea(Circle(3.0)); printArea(Square(4.0)); }";
    expectOutput("virtual_dispatch_through_facet", src, {"9.0", "16.0"});
}

void test_virtual_dispatch_via_override() {
    std::string src =
        "mold Base {\n"
        "    virtual fn describe() -> string { return \"base\"; }\n"
        "}\n"
        "mold Derived : Base {\n"
        "    override fn describe() -> string { return \"derived\"; }\n"
        "}\n"
        "fn announce(b: Base) { print(b.describe()); }\n"
        "fn main() { announce(Base()); announce(Derived()); }";
    expectOutput("virtual_dispatch_via_override", src, {"base", "derived"});
}

// --- Наследование: поля, this/super ---

void test_inherited_field_and_method() {
    std::string src =
        "mold Animal {\n"
        "    let name: string\n"
        "    Animal(n: string) { name = n; }\n"
        "    fn describe() -> string { return name; }\n"
        "}\n"
        "mold Dog : Animal {\n"
        "    let breed: string\n"
        "    Dog(n: string, b: string) {\n"
        "        super(n)\n"
        "        breed = b\n"
        "    }\n"
        "}\n"
        "fn main() { let d = Dog(\"Rex\", \"Labrador\"); print(d.name); print(d.describe()); print(d.breed); }";
    expectOutput("inherited_field_and_method", src, {"Rex", "Rex", "Labrador"});
}

void test_super_method_call() {
    std::string src =
        "mold Base {\n"
        "    virtual fn greet() -> string { return \"base\"; }\n"
        "}\n"
        "mold Derived : Base {\n"
        "    override fn greet() -> string { return super.greet() + \"-derived\"; }\n"
        "}\n"
        "fn main() { print(Derived().greet()); }";
    expectOutput("super_method_call", src, {"base-derived"});
}

void test_this_reference_in_method() {
    std::string src =
        "mold Box {\n"
        "    let value: int\n"
        "    Box(v: int) { value = v; }\n"
        "    fn getSelf() -> Box { return this; }\n"
        "}\n"
        "fn main() { let b = Box(42); print(b.getSelf().value); }";
    expectOutput("this_reference_in_method", src, {"42"});
}

// --- GC ---

void test_gc_reclaims_unreachable_objects() {
    std::string src =
        "mold Node {\n"
        "    let value: int\n"
        "    Node(v: int) { value = v; }\n"
        "}\n"
        "fn main() {\n"
        "    let i = 0\n"
        "    while i < 1000 {\n"
        "        let n = Node(i)\n"
        "        i += 1\n"
        "    }\n"
        "    print(i)\n"
        "}\n";
    auto result = runSource(src);
    CHECK(result.output == std::vector<std::string>{"1000"});
    // 1000 Node были аллоцированы, но слот 'n' переиспользуется на каждой
    // итерации — предыдущие становятся мусором. Живых объектов должно
    // остаться на порядки меньше 1000, если GC реально собирает мусор.
    if (result.liveObjectsAfterRun >= 100) {
        std::cerr << "FAILED: gc_reclaims_unreachable_objects — живых объектов после run(): "
                  << result.liveObjectsAfterRun << " (ожидалось значительно меньше 100)\n";
    }
    CHECK(result.liveObjectsAfterRun < 100);
}

void test_gc_keeps_reachable_object() {
    std::string src =
        "mold Node {\n"
        "    let value: int\n"
        "    Node(v: int) { value = v; }\n"
        "}\n"
        "fn main() {\n"
        "    let keep = Node(999)\n"
        "    let i = 0\n"
        "    while i < 500 {\n"
        "        let throwaway = Node(i)\n"
        "        i += 1\n"
        "    }\n"
        "    print(keep.value)\n"
        "}\n";
    // Если бы GC ошибочно собрал 'keep' (например, не просканировав нужный
    // слот стека как корень), доступ к keep.value упал бы в runtime-ошибку
    // (null) — сам факт корректного вывода уже доказывает, что 'keep'
    // пережил все промежуточные сборки мусора.
    expectOutput("gc_keeps_reachable_object", src, {"999"});
}

// --- Массивы и GC ---

void test_gc_reclaims_objects_reachable_only_through_discarded_array() {
    std::string src =
        "mold Node {\n"
        "    let value: int\n"
        "    Node(v: int) { value = v; }\n"
        "}\n"
        "fn main() {\n"
        "    let i = 0\n"
        "    while i < 200 {\n"
        "        let batch = [Node(i), Node(i), Node(i)]\n"
        "        i += 1\n"
        "    }\n"
        "    print(i)\n"
        "}\n";
    auto result = runSource(src);
    CHECK(result.output == std::vector<std::string>{"200"});
    // 200 итераций * (1 массив + 3 Node) — но 'batch' переиспользуется
    // каждую итерацию, так что и массив, и все три Node в нём должны
    // собираться как мусор. Проверяет, что markValue не держит элементы
    // массива живыми дольше самого массива.
    if (result.liveObjectsAfterRun >= 100) {
        std::cerr << "FAILED: gc_reclaims_objects_reachable_only_through_discarded_array — живых объектов: "
                  << result.liveObjectsAfterRun << " (ожидалось значительно меньше 100)\n";
    }
    CHECK(result.liveObjectsAfterRun < 100);
}

void test_gc_keeps_objects_reachable_only_through_array() {
    std::string src =
        "mold Node {\n"
        "    let value: int\n"
        "    Node(v: int) { value = v; }\n"
        "}\n"
        "fn main() {\n"
        "    let keep = [Node(7), Node(8), Node(9)]\n"
        "    let i = 0\n"
        "    while i < 500 {\n"
        "        let throwaway = Node(i)\n"
        "        i += 1\n"
        "    }\n"
        "    print(keep[0].value)\n"
        "    print(keep[1].value)\n"
        "    print(keep[2].value)\n"
        "}\n";
    // Единственная ссылка на эти три Node — через массив 'keep'. Если бы
    // markValue не трассировал элементы массива, все три собрались бы при
    // первой же автоматической сборке внутри цикла, и .value упал бы в
    // runtime-ошибку на null вместо печати значений.
    expectOutput("gc_keeps_objects_reachable_only_through_array", src, {"7", "8", "9"});
}

void test_array_as_object_field_survives_with_object() {
    std::string src =
        "mold Bag {\n"
        "    let items: [int]\n"
        "    Bag(a: int, b: int) { items = [a, b]; }\n"
        "}\n"
        "fn main() {\n"
        "    let bag = Bag(11, 22)\n"
        "    let i = 0\n"
        "    while i < 500 {\n"
        "        let throwaway = Bag(i, i)\n"
        "        i += 1\n"
        "    }\n"
        "    print(bag.items[0])\n"
        "    print(bag.items[1])\n"
        "}\n";
    // Массив висит на поле объекта, не лежит на стеке напрямую — другой
    // путь трассировки: markValue находит 'bag' как объект, рекурсирует в
    // его fields, там натыкается на массив и рекурсирует уже в его
    // elements. Проверяет, что обе ветки markValue (isObject/isArray)
    // действительно связаны рекурсией, а не работают только по отдельности.
    expectOutput("array_as_object_field_survives_with_object", src, {"11", "22"});
}

} // namespace

int main() {
    test_construction_and_field_read();
    test_field_write_and_compound_assignment();
    test_field_default_values();
    test_field_initializer_runs();

    test_non_virtual_method_call();
    test_virtual_dispatch_through_facet();
    test_virtual_dispatch_via_override();

    test_inherited_field_and_method();
    test_super_method_call();
    test_this_reference_in_method();

    test_gc_reclaims_unreachable_objects();
    test_gc_keeps_reachable_object();

    test_gc_reclaims_objects_reachable_only_through_discarded_array();
    test_gc_keeps_objects_reachable_only_through_array();
    test_array_as_object_field_survives_with_object();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

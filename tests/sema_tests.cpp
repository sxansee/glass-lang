#include <iostream>
#include <string>

#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "../src/sema/Sema.h"
#include "test_framework.h"

using namespace glass;

namespace {

std::vector<std::string> analyzeSource(const std::string& src) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        std::vector<std::string> errs = {"[СНАЧАЛА НЕ РАЗОБРАЛОСЬ]"};
        for (auto& e : parser.errors()) errs.push_back(e);
        return errs;
    }
    Sema sema;
    return sema.analyze(prog);
}

void expectClean(const std::string& name, const std::string& src) {
    auto errs = analyzeSource(src);
    if (!errs.empty()) {
        std::cerr << "FAILED: " << name << " ожидал 0 ошибок, получил " << errs.size() << ":\n";
        for (auto& e : errs) std::cerr << "    " << e << "\n";
    }
    CHECK(errs.empty());
}

void expectErrorContaining(const std::string& name, const std::string& src, const std::string& needle) {
    auto errs = analyzeSource(src);
    bool found = false;
    for (auto& e : errs) {
        if (e.find(needle) != std::string::npos) found = true;
    }
    if (!found) {
        std::cerr << "FAILED: " << name << " ожидал ошибку с подстрокой '" << needle
                  << "', получил " << errs.size() << " ошибок:\n";
        for (auto& e : errs) std::cerr << "    " << e << "\n";
    }
    CHECK(found);
}

// --- Базовые сценарии ---

void test_minimal_valid_program() {
    expectClean("minimal_valid_program", "fn main() { }");
}

void test_duplicate_top_level_name() {
    expectErrorContaining("duplicate_top_level_name",
                           "mold Foo { }\nfacet Foo { }\nfn main() { }",
                           "повторное объявление имени 'Foo'");
}

// --- Наследование ---

void test_unknown_base_type() {
    expectErrorContaining("unknown_base_type", "mold A : Ghost { }\nfn main() { }",
                           "неизвестный базовый тип 'Ghost'");
}

void test_multiple_mold_bases_rejected() {
    expectErrorContaining("multiple_mold_bases_rejected",
                           "mold A { }\nmold B { }\nmold C : A, B { }\nfn main() { }",
                           "больше одного базового mold'а");
}

void test_facet_cannot_extend_mold() {
    expectErrorContaining("facet_cannot_extend_mold",
                           "mold A { }\nfacet F : A { }\nfn main() { }",
                           "не может наследовать mold");
}

void test_mold_inheritance_cycle_detected() {
    expectErrorContaining("mold_inheritance_cycle_detected",
                           "mold A : B { }\nmold B : A { }\nfn main() { }",
                           "цикл в цепочке наследования mold'ов");
}

void test_facet_inheritance_cycle_detected() {
    expectErrorContaining("facet_inheritance_cycle_detected",
                           "facet A : B { }\nfacet B : A { }\nfn main() { }",
                           "цикл в цепочке наследования facet'ов");
}

// --- Facet-контракты ---

void test_facet_contract_satisfied_directly() {
    expectClean("facet_contract_satisfied_directly",
                "facet Shape {\n    fn area() -> double;\n}\n"
                "mold Circle : Shape {\n    fn area() -> double { return 0; }\n}\n"
                "fn main() { }");
}

void test_facet_contract_missing_method_is_error() {
    expectErrorContaining("facet_contract_missing_method_is_error",
                           "facet Shape {\n    fn area() -> double;\n}\n"
                           "mold Circle : Shape { }\n"
                           "fn main() { }",
                           "не предоставляет метод 'area'");
}

void test_facet_contract_satisfied_via_inherited_method() {
    // Square наследует area() от Base — не обязан переопределять её сам
    expectClean("facet_contract_satisfied_via_inherited_method",
                "facet Shape {\n    fn area() -> double;\n}\n"
                "mold Base : Shape {\n    fn area() -> double { return 0; }\n}\n"
                "mold Square : Base { }\n"
                "fn main() { }");
}

void test_facet_contract_wrong_signature_is_error() {
    // area() есть, но возвращает int, а не double — сигнатура не совпадает
    expectErrorContaining("facet_contract_wrong_signature_is_error",
                           "facet Shape {\n    fn area() -> double;\n}\n"
                           "mold Circle : Shape {\n    fn area() -> int { return 0; }\n}\n"
                           "fn main() { }",
                           "не предоставляет метод 'area'");
}

// --- Конструкторы ---

void test_constructor_name_must_match_mold() {
    expectErrorContaining("constructor_name_must_match_mold",
                           "mold Circle {\n    Square() { }\n}\nfn main() { }",
                           "не совпадает с именем mold'а");
}

void test_multiple_constructors_rejected() {
    expectErrorContaining("multiple_constructors_rejected",
                           "mold Circle {\n    Circle() { }\n    Circle(x: int) { }\n}\nfn main() { }",
                           "только один конструктор");
}

// --- override ---

void test_override_with_matching_base_is_clean() {
    expectClean("override_with_matching_base_is_clean",
                "mold Scene {\n    virtual fn onFrame() { }\n}\n"
                "mold Game : Scene {\n    override fn onFrame() { }\n}\n"
                "fn main() { }");
}

void test_override_without_base_method_is_error() {
    expectErrorContaining("override_without_base_method_is_error",
                           "mold Game {\n    override fn onFrame() { }\n}\nfn main() { }",
                           "override, но в базовом mold'е нет");
}

// --- Прочие проверки членов ---

void test_duplicate_member_name_is_error() {
    expectErrorContaining("duplicate_member_name_is_error",
                           "mold Foo {\n    let x: int\n    fn x() { }\n}\nfn main() { }",
                           "повторное имя члена 'x'");
}

// --- Точка входа ---

void test_missing_main_is_error() {
    expectErrorContaining("missing_main_is_error", "mold Foo { }", "точка входа");
}

void test_main_with_params_is_error() {
    expectErrorContaining("main_with_params_is_error", "fn main(x: int) { }",
                           "не должна принимать параметры");
}

void test_main_with_return_type_is_error() {
    expectErrorContaining("main_with_return_type_is_error", "fn main() -> int { return 0; }",
                           "не должна иметь возвращаемый тип");
}

// --- Типы в сигнатурах ---

void test_unknown_type_in_field_is_error() {
    expectErrorContaining("unknown_type_in_field_is_error",
                           "mold Foo {\n    let x: Ghost\n}\nfn main() { }", "неизвестный тип 'Ghost'");
}

void test_generic_param_is_not_unknown_type() {
    expectClean("generic_param_is_not_unknown_type",
                "mold Box<T> {\n    let value: T\n}\nfn main() { }");
}

void test_unknown_type_in_function_param_is_error() {
    expectErrorContaining("unknown_type_in_function_param_is_error",
                           "fn f(x: Ghost) { }\nfn main() { }", "неизвестный тип 'Ghost'");
}

// --- Комплексные сквозные сценарии ---

void test_full_grammar_example_is_clean() {
    std::string src =
        "facet Shape {\n"
        "    fn area() -> double;\n"
        "}\n"
        "\n"
        "mold Circle : Shape {\n"
        "    let radius: double\n"
        "\n"
        "    Circle(r: double) {\n"
        "        radius = r\n"
        "    }\n"
        "\n"
        "    fn area() -> double {\n"
        "        return 3.14159 * radius * radius\n"
        "    }\n"
        "}\n"
        "\n"
        "mold Square : Shape {\n"
        "    let side: double\n"
        "\n"
        "    Square(s: double) {\n"
        "        side = s\n"
        "    }\n"
        "\n"
        "    fn area() -> double {\n"
        "        return side * side\n"
        "    }\n"
        "}\n"
        "\n"
        "fn totalArea(shapes: [Shape]) -> double {\n"
        "    let total: double = 0\n"
        "    for s in shapes {\n"
        "        total += s.area()\n"
        "    }\n"
        "    return total\n"
        "}\n"
        "\n"
        "fn main() {\n"
        "    let shapes: [Shape] = [Circle(3.0), Square(4.0)]\n"
        "    print(totalArea(shapes))\n"
        "}\n";
    expectClean("full_grammar_example_is_clean", src);
}

void test_scene_game_override_scenario_is_clean() {
    std::string src =
        "mold Canvas { }\n"
        "mold Color {\n"
        "    Color(r: int, g: int, b: int) { }\n"
        "}\n"
        "mold Scene {\n"
        "    virtual fn onFrame(canvas: Canvas) { }\n"
        "    virtual fn onMouseMove(x: double, y: double) { }\n"
        "}\n"
        "mold Game : Scene {\n"
        "    let x: double\n"
        "    let y: double\n"
        "\n"
        "    override fn onFrame(canvas: Canvas) {\n"
        "        canvas.clear()\n"
        "    }\n"
        "\n"
        "    override fn onMouseMove(x: double, y: double) {\n"
        "        this.x = x\n"
        "        this.y = y\n"
        "    }\n"
        "}\n"
        "fn main() {\n"
        "    run(Game())\n"
        "}\n";
    expectClean("scene_game_override_scenario_is_clean", src);
}

// --- virtual / override: правила диспетчеризации ---
// override допустим только над методом, который реально виртуален; метод без override поверх
// virtual-метода базы — ошибка (иначе он молча остаётся мёртвым кодом вне vtable).

const char* kVirtualBase = "mold Base {\n    virtual fn who() -> string { return \"base\" }\n}\n";

void test_override_over_virtual_is_clean() {
    expectClean("override_over_virtual_is_clean",
                std::string(kVirtualBase) +
                    "mold Leaf : Base {\n    override fn who() -> string { return \"leaf\" }\n}\nfn main() { }");
}

void test_override_chain_is_clean() {
    expectClean("override_chain_is_clean",
                std::string(kVirtualBase) +
                    "mold Mid : Base {\n    override fn who() -> string { return \"mid\" }\n}\n"
                    "mold Leaf : Mid {\n    override fn who() -> string { return \"leaf\" }\n}\nfn main() { }");
}

void test_override_over_non_virtual_is_error() {
    expectErrorContaining("override_over_non_virtual_is_error",
                          "mold Base {\n    fn who() -> string { return \"base\" }\n}\n"
                          "mold Leaf : Base {\n    override fn who() -> string { return \"leaf\" }\n}\nfn main() { }",
                          "одноимённый метод не virtual");
}

void test_override_over_facet_implementation_is_clean() {
    // Метод, реализующий facet-контракт, виртуален сам по себе — override над ним законен.
    expectClean("override_over_facet_implementation_is_clean",
                "facet Shape {\n    fn area() -> double;\n}\n"
                "mold Circle : Shape {\n    fn area() -> double { return 3.0 }\n}\n"
                "mold Big : Circle {\n    override fn area() -> double { return 9.0 }\n}\nfn main() { }");
}

void test_override_over_inherited_facet_contract_is_clean() {
    // Контракт объявлен в базовом facet'е: Cube реализует Solid, а Solid наследует Shape.
    expectClean("override_over_inherited_facet_contract_is_clean",
                "facet Shape {\n    fn area() -> double;\n}\n"
                "facet Solid : Shape {\n}\n"
                "mold Cube : Solid {\n    fn area() -> double { return 6.0 }\n}\n"
                "mold Big : Cube {\n    override fn area() -> double { return 9.0 }\n}\nfn main() { }");
}

void test_override_over_tostring_is_clean() {
    // toString() без параметров неявно виртуален — override над обычным toString законен.
    expectClean("override_over_tostring_is_clean",
                "mold A {\n    fn toString() -> string { return \"a\" }\n}\n"
                "mold B : A {\n    override fn toString() -> string { return \"b\" }\n}\nfn main() { }");
}

void test_plain_tostring_in_derived_is_clean() {
    expectClean("plain_tostring_in_derived_is_clean",
                "mold A {\n    fn toString() -> string { return \"a\" }\n}\n"
                "mold B : A {\n    fn toString() -> string { return \"b\" }\n}\nfn main() { }");
}

void test_missing_override_over_virtual_is_error() {
    expectErrorContaining("missing_override_over_virtual_is_error",
                          std::string(kVirtualBase) +
                              "mold Leaf : Base {\n    fn who() -> string { return \"leaf\" }\n}\nfn main() { }",
                          "добавьте override");
}

void test_missing_override_over_override_is_error() {
    expectErrorContaining("missing_override_over_override_is_error",
                          std::string(kVirtualBase) +
                              "mold Mid : Base {\n    override fn who() -> string { return \"mid\" }\n}\n"
                              "mold Leaf : Mid {\n    fn who() -> string { return \"leaf\" }\n}\nfn main() { }",
                          "добавьте override");
}

void test_missing_override_over_facet_implementation_is_error() {
    expectErrorContaining("missing_override_over_facet_implementation_is_error",
                          "facet Shape {\n    fn area() -> double;\n}\n"
                          "mold Circle : Shape {\n    fn area() -> double { return 3.0 }\n}\n"
                          "mold Big : Circle {\n    fn area() -> double { return 9.0 }\n}\nfn main() { }",
                          "добавьте override");
}

void test_plain_hiding_over_non_virtual_is_clean() {
    // Сокрытие обычного метода обычным допустимо: вызов идёт по статическому типу (проверено в language_tests).
    expectClean("plain_hiding_over_non_virtual_is_clean",
                "mold Base {\n    fn who() -> string { return \"base\" }\n}\n"
                "mold Leaf : Base {\n    fn who() -> string { return \"leaf\" }\n}\nfn main() { }");
}

void test_plain_method_implementing_own_facet_is_clean() {
    // Leaf сам реализует facet Shape → его area() виртуален и в vtable переопределяет базовый.
    expectClean("plain_method_implementing_own_facet_is_clean",
                "facet Shape {\n    fn area() -> double;\n}\n"
                "mold Base {\n    virtual fn area() -> double { return 1.0 }\n}\n"
                "mold Leaf : Base, Shape {\n    fn area() -> double { return 2.0 }\n}\nfn main() { }");
}

} // namespace

int main() {
    test_minimal_valid_program();
    test_duplicate_top_level_name();

    test_unknown_base_type();
    test_multiple_mold_bases_rejected();
    test_facet_cannot_extend_mold();
    test_mold_inheritance_cycle_detected();
    test_facet_inheritance_cycle_detected();

    test_facet_contract_satisfied_directly();
    test_facet_contract_missing_method_is_error();
    test_facet_contract_satisfied_via_inherited_method();
    test_facet_contract_wrong_signature_is_error();

    test_constructor_name_must_match_mold();
    test_multiple_constructors_rejected();

    test_override_with_matching_base_is_clean();
    test_override_without_base_method_is_error();
    test_override_over_virtual_is_clean();
    test_override_chain_is_clean();
    test_override_over_non_virtual_is_error();
    test_override_over_facet_implementation_is_clean();
    test_override_over_inherited_facet_contract_is_clean();
    test_override_over_tostring_is_clean();
    test_plain_tostring_in_derived_is_clean();
    test_missing_override_over_virtual_is_error();
    test_missing_override_over_override_is_error();
    test_missing_override_over_facet_implementation_is_error();
    test_plain_hiding_over_non_virtual_is_clean();
    test_plain_method_implementing_own_facet_is_clean();

    test_duplicate_member_name_is_error();

    test_missing_main_is_error();
    test_main_with_params_is_error();
    test_main_with_return_type_is_error();

    test_unknown_type_in_field_is_error();
    test_generic_param_is_not_unknown_type();
    test_unknown_type_in_function_param_is_error();

    test_full_grammar_example_is_clean();
    test_scene_game_override_scenario_is_clean();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

#include <iostream>
#include <string>

#include "../src/ast/AstPrinter.h"
#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "test_framework.h"

using namespace glass;

namespace {

// Разбирает source как отдельное выражение (обёрнутое в "fn f() { <expr>; }"),
// чтобы не дублировать в каждом тесте бойлерплейт объявления функции.
std::string parseExprToString(const std::string& exprSrc) {
    Lexer lexer("fn f() { " + exprSrc + "; }");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        std::string msg = "PARSE ERRORS: ";
        for (auto& e : parser.errors()) msg += e + " | ";
        return msg;
    }
    auto& fn = std::get<FunctionDecl>(prog.declarations[0]);
    auto& stmt = fn.body->statements[0];
    AstPrinter printer;
    return printer.print(*stmt);
}

std::string parseStmtToString(const std::string& stmtSrc) {
    Lexer lexer("fn f() { " + stmtSrc + " }");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) {
        std::string msg = "PARSE ERRORS: ";
        for (auto& e : parser.errors()) msg += e + " | ";
        return msg;
    }
    auto& fn = std::get<FunctionDecl>(prog.declarations[0]);
    auto& stmt = fn.body->statements[0];
    AstPrinter printer;
    return printer.print(*stmt);
}

// --- Приоритет и ассоциативность операций (раздел 7 грамматики) ---

void test_precedence_mul_over_add() {
    CHECK_EQ(parseExprToString("1 + 2 * 3"),
             std::string("(exprstmt (binary + (int 1) (binary * (int 2) (int 3))))"));
}

void test_precedence_relational_over_equality() {
    CHECK_EQ(parseExprToString("a == b < c"),
             std::string("(exprstmt (binary == (id a) (binary < (id b) (id c))))"));
}

void test_precedence_and_over_or() {
    CHECK_EQ(parseExprToString("a || b && c"),
             std::string("(exprstmt (binary || (id a) (binary && (id b) (id c))))"));
}

void test_left_associativity_of_additive() {
    CHECK_EQ(parseExprToString("1 - 2 - 3"),
             std::string("(exprstmt (binary - (binary - (int 1) (int 2)) (int 3)))"));
}

void test_right_associativity_of_assignment() {
    CHECK_EQ(parseExprToString("a = b = 5"),
             std::string("(exprstmt (assign = (id a) (assign = (id b) (int 5))))"));
}

void test_unary_and_postfix_precedence() {
    CHECK_EQ(parseExprToString("-a.b"),
             std::string("(exprstmt (unary - (field (id a) .b)))"));
}

// --- Неоднозначности: приведение типа vs скобки, дженерики vs сравнение ---

void test_cast_of_identifier() {
    CHECK_EQ(parseExprToString("(int)x"),
             std::string("(exprstmt (cast int (id x)))"));
}

void test_grouping_not_confused_with_cast() {
    // (x) — просто группировка, x не тип, а x+1 сразу за скобкой это подтверждает
    CHECK_EQ(parseExprToString("(x + 1) * 2"),
             std::string("(exprstmt (binary * (binary + (id x) (int 1)) (int 2)))"));
}

void test_generic_call_not_confused_with_comparison() {
    CHECK_EQ(parseExprToString("identity<int>(5)"),
             std::string("(exprstmt (call (id identity int) (int 5)))"));
}

void test_plain_chained_comparison_still_works() {
    // без круглых скобок дальше и без чего-то, что выглядело бы как Type,
    // жадная попытка дженериков откатывается, и это обычное сравнение
    CHECK_EQ(parseExprToString("a < b && b < c"),
             std::string("(exprstmt (binary && (binary < (id a) (id b)) (binary < (id b) (id c))))"));
}

// --- Постфиксная цепочка ---

void test_postfix_chain_field_call_index_introspect() {
    CHECK_EQ(parseExprToString("shapes[0]~vtable"),
             std::string("(exprstmt (introspect (index (id shapes) (int 0)) ~vtable))"));
}

void test_method_call_chain() {
    CHECK_EQ(parseExprToString("a.b().c"),
             std::string("(exprstmt (field (call (field (id a) .b)) .c))"));
}

void test_construction_without_new() {
    CHECK_EQ(parseExprToString("Circle(3.0)"),
             std::string("(exprstmt (call (id Circle) (double 3)))"));
}

void test_array_literal() {
    CHECK_EQ(parseExprToString("[Circle(3.0), Square(4.0)]"),
             std::string("(exprstmt (array (call (id Circle) (double 3)) (call (id Square) (double 4))))"));
}

void test_global_introspect() {
    CHECK_EQ(parseExprToString("~heap"), std::string("(exprstmt (~heap))"));
}

// --- Операторы ---

void test_let_with_explicit_type() {
    CHECK_EQ(parseStmtToString("let x: int = 5;"), std::string("(let x:int = (int 5))"));
}

void test_let_with_inferred_type() {
    CHECK_EQ(parseStmtToString("let x = 5;"), std::string("(let x = (int 5))"));
}

void test_if_else_if_chain() {
    CHECK_EQ(parseStmtToString("if a { x(); } else if b { y(); } else { z(); }"),
             std::string("(if (id a) (block (exprstmt (call (id x)))) else "
                          "(if (id b) (block (exprstmt (call (id y)))) else "
                          "(block (exprstmt (call (id z))))))"));
}

void test_for_over_iterable() {
    CHECK_EQ(parseStmtToString("for s in shapes { total += s.area(); }"),
             std::string("(for s in (id shapes) (block (exprstmt (assign += (id total) "
                          "(call (field (id s) .area))))))"));
}

void test_for_range() {
    CHECK_EQ(parseStmtToString("for i in 0..10 { print(i); }"),
             std::string("(for i in (int 0)..(int 10) (block (exprstmt (call (id print) (id i)))))"));
}

void test_try_catch_finally() {
    CHECK_EQ(parseStmtToString("try { risky(); } catch e: Exception { log(e); } finally { cleanup(); }"),
             std::string("(try (block (exprstmt (call (id risky)))) "
                          "(catch e:Exception (block (exprstmt (call (id log) (id e))))) "
                          "(finally (block (exprstmt (call (id cleanup))))))"));
}

void test_empty_statement_from_asi_after_brace() {
    // ASI вставляет ';' после '}' блока if — парсер должен молча его принять
    Lexer lexer("fn f() {\n    if true {\n        x();\n    }\n    y();\n}");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    auto& fn = std::get<FunctionDecl>(prog.declarations[0]);
    CHECK_EQ(fn.body->statements.size(), size_t(3));  // if-stmt, EmptyStmt, y()
}

// --- Декларации ---

void test_facet_declaration() {
    Lexer lexer("facet Shape {\n    fn area() -> double;\n}");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    CHECK_EQ(prog.declarations.size(), size_t(1));
    auto& facet = std::get<FacetDecl>(prog.declarations[0]);
    CHECK_EQ(facet.name, std::string("Shape"));
    CHECK_EQ(facet.methods.size(), size_t(1));
    CHECK_EQ(facet.methods[0].name, std::string("area"));
}

void test_mold_with_field_ctor_method_and_base() {
    std::string src =
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
        "}\n";
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    auto& mold = std::get<MoldDecl>(prog.declarations[0]);
    CHECK_EQ(mold.name, std::string("Circle"));
    CHECK_EQ(mold.baseTypes.size(), size_t(1));
    CHECK_EQ(mold.baseTypes[0], std::string("Shape"));
    CHECK_EQ(mold.members.size(), size_t(3));
    CHECK(std::holds_alternative<FieldDecl>(mold.members[0]));
    CHECK(std::holds_alternative<ConstructorDecl>(mold.members[1]));
    CHECK(std::holds_alternative<MethodDecl>(mold.members[2]));
}

void test_virtual_override_modifiers() {
    std::string src =
        "mold Scene {\n"
        "    virtual fn onFrame(canvas: Canvas) { }\n"
        "}\n"
        "mold Game : Scene {\n"
        "    override fn onFrame(canvas: Canvas) { }\n"
        "}\n";
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    auto& scene = std::get<MoldDecl>(prog.declarations[0]);
    auto& sceneMethod = std::get<MethodDecl>(scene.members[0]);
    CHECK(sceneMethod.modifier == MethodModifier::Virtual);
    auto& game = std::get<MoldDecl>(prog.declarations[1]);
    auto& gameMethod = std::get<MethodDecl>(game.members[0]);
    CHECK(gameMethod.modifier == MethodModifier::Override);
}

void test_generic_mold_declaration() {
    Lexer lexer("mold Box<T> {\n    let value: T\n}");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    auto& box = std::get<MoldDecl>(prog.declarations[0]);
    CHECK_EQ(box.genericParams.size(), size_t(1));
    CHECK_EQ(box.genericParams[0], std::string("T"));
}

void test_array_and_generic_types() {
    Lexer lexer("fn totalArea(shapes: [Shape]) -> double { return 0; }");
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(parser.errors().empty());
    auto& fn = std::get<FunctionDecl>(prog.declarations[0]);
    CHECK_EQ(fn.params[0].type.kind == TypeNode::Kind::Array, true);
    CHECK_EQ(typeToString(fn.params[0].type), std::string("[Shape]"));
}

// --- Полный пример из grammar.md ---

void test_full_example_from_grammar_parses_cleanly() {
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
        "\n"
        "    try {\n"
        "        print(totalArea(shapes))\n"
        "        print(shapes[0]~vtable)\n"
        "        print(shapes[0]~address)\n"
        "    } catch e: Exception {\n"
        "        print(\"Ошибка: \" + e.message)\n"
        "    }\n"
        "}\n";
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    for (auto& e : parser.errors()) std::cerr << "  unexpected error: " << e << "\n";
    CHECK(parser.errors().empty());
    CHECK_EQ(prog.declarations.size(), size_t(5));  // Shape, Circle, Square, totalArea, main
}

// --- Восстановление после ошибок ---

void test_error_recovery_within_block_finds_next_valid_statement() {
    // '@' — символ, которого нет в языке; лексер вернёт Error-токены.
    // После восстановления парсер обязан найти и разобрать y() дальше.
    std::string src = "fn f() {\n    @@@;\n    y();\n}";
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    CHECK(!parser.errors().empty());
    auto& fn = std::get<FunctionDecl>(prog.declarations[0]);
    bool foundY = false;
    for (auto& st : fn.body->statements) {
        if (auto* es = dynamic_cast<ExprStmt*>(st.get())) {
            if (auto* call = dynamic_cast<CallExpr*>(es->expr.get())) {
                if (auto* id = dynamic_cast<IdentifierExpr*>(call->callee.get())) {
                    if (id->name == "y") foundY = true;
                }
            }
        }
    }
    CHECK(foundY);
}

void test_error_recovery_terminates_on_unclosed_mold() {
    // mold без закрывающей '}' — критично, чтобы парсер корректно
    // завершился на EOF, а не завис в synchronize().
    std::string src = "mold Broken {\n    let x: int\n";
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();  // если бы был баг из синхронизации — тест зависнет здесь
    CHECK(!parser.errors().empty());
}

} // namespace

int main() {
    test_precedence_mul_over_add();
    test_precedence_relational_over_equality();
    test_precedence_and_over_or();
    test_left_associativity_of_additive();
    test_right_associativity_of_assignment();
    test_unary_and_postfix_precedence();

    test_cast_of_identifier();
    test_grouping_not_confused_with_cast();
    test_generic_call_not_confused_with_comparison();
    test_plain_chained_comparison_still_works();

    test_postfix_chain_field_call_index_introspect();
    test_method_call_chain();
    test_construction_without_new();
    test_array_literal();
    test_global_introspect();

    test_let_with_explicit_type();
    test_let_with_inferred_type();
    test_if_else_if_chain();
    test_for_over_iterable();
    test_for_range();
    test_try_catch_finally();
    test_empty_statement_from_asi_after_brace();

    test_facet_declaration();
    test_mold_with_field_ctor_method_and_base();
    test_virtual_override_modifiers();
    test_generic_mold_declaration();
    test_array_and_generic_types();

    test_full_example_from_grammar_parses_cleanly();

    test_error_recovery_within_block_finds_next_valid_statement();
    test_error_recovery_terminates_on_unclosed_mold();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

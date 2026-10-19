#include <iostream>
#include <string>

#include "../src/lexer/Lexer.h"
#include "test_framework.h"

using glass::Lexer;
using glass::Token;
using glass::TokenType;

namespace {

std::string typeSeq(const std::vector<Token>& tokens) {
    std::string s;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (i) s += " ";
        s += glass::tokenTypeName(tokens[i].type);
    }
    return s;
}

void test_keywords_and_identifiers() {
    Lexer lex("mold Circle facet Shape fn let virtual override");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens),
             std::string("mold Identifier facet Identifier fn let virtual override EOF"));
}

void test_numbers() {
    Lexer lex("42 3.14 2.5e10 7e-3 100");
    auto tokens = lex.tokenize();
    CHECK(tokens[0].type == TokenType::IntLiteral);
    CHECK_EQ(tokens[0].intValue, 42LL);
    CHECK(tokens[1].type == TokenType::DoubleLiteral);
    CHECK_EQ(tokens[1].doubleValue, 3.14);
    CHECK(tokens[2].type == TokenType::DoubleLiteral);
    CHECK(tokens[3].type == TokenType::DoubleLiteral);
    CHECK(tokens[4].type == TokenType::IntLiteral);
    CHECK_EQ(tokens[4].intValue, 100LL);
}

void test_string_escapes() {
    Lexer lex(R"("hello\nworld" "tab\there" "quote\"inside")");
    auto tokens = lex.tokenize();
    CHECK(tokens[0].type == TokenType::StringLiteral);
    CHECK_EQ(tokens[0].stringValue, std::string("hello\nworld"));
    CHECK(tokens[1].type == TokenType::StringLiteral);
    CHECK_EQ(tokens[1].stringValue, std::string("tab\there"));
    CHECK(tokens[2].type == TokenType::StringLiteral);
    CHECK_EQ(tokens[2].stringValue, std::string("quote\"inside"));
}

void test_multichar_operators() {
    Lexer lex("-> .. == != <= >= && || += -= *= /=");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens),
             std::string("-> .. == != <= >= && || += -= *= /= EOF"));
}

void test_introspection_tokens() {
    Lexer lex("d~vtable d~layout d~address ~heap ~gc");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens),
             std::string("Identifier ~ vtable Identifier ~ layout Identifier ~ address ~ heap ~ gc ; EOF"));
}

void test_comments_are_skipped() {
    Lexer lex("let x = 5 // однострочный комментарий\n"
               "/* блочный\n"
               "   комментарий */\n"
               "let y = 1");
    auto tokens = lex.tokenize();
    CHECK(typeSeq(tokens).find("ERROR") == std::string::npos);
    // оба let должны быть на месте
    int letCount = 0;
    for (auto& t : tokens) if (t.type == TokenType::Let) letCount++;
    CHECK_EQ(letCount, 2);
}

// --- ASI: главная часть, ради которой лексер сложнее обычного ---

void test_asi_inserts_after_identifier_and_literal() {
    Lexer lex("let x = 5\nlet y = 10");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens),
             std::string("let Identifier = IntLiteral ; let Identifier = IntLiteral ; EOF"));
}

void test_asi_no_insert_after_operator() {
    // строка заканчивается на '=' — выражение продолжается дальше, ';' не нужен
    Lexer lex("let x =\n5");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens), std::string("let Identifier = IntLiteral ; EOF"));
}

void test_asi_no_insert_after_comma_or_openparen() {
    // ',' и '(' сами по себе не ASI-триггеры
    Lexer lex("foo(1, 2)");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens), std::string("Identifier ( IntLiteral , IntLiteral ) ; EOF"));
}

void test_asi_multiline_call_needs_trailing_comma() {
    // Как и в Go: если перенести последний аргумент и ')' на новую строку
    // БЕЗ запятой после него, ASI вставит ';' прямо перед ')' — это
    // осознанная, задокументированная особенность (см. glass-grammar.md,
    // раздел 1), а не баг. Идиоматично ставить trailing comma при переносе.
    Lexer lex("foo(\n    1,\n    2,\n)");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens),
             std::string("Identifier ( IntLiteral , IntLiteral , ) ; EOF"));
}

void test_asi_return_without_value_on_same_line() {
    // осознанная особенность (как в Go): перенос строки сразу после
    // return завершает оператор без значения
    Lexer lex("return\n5");
    auto tokens = lex.tokenize();
    CHECK_EQ(typeSeq(tokens), std::string("return ; IntLiteral ; EOF"));
}

// '}' + перевод строки + else/catch/finally: ';' не вставляется (стиль с else на новой строке).
bool semicolonBefore(const std::vector<Token>& tokens, TokenType type) {
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i].type == type) return tokens[i - 1].type == TokenType::Semicolon;
    }
    return false;
}

void test_asi_suppressed_before_else_catch_finally() {
    {
        Lexer lex("if a {\n}\nelse {\n}\n");
        auto tokens = lex.tokenize();
        CHECK(!semicolonBefore(tokens, TokenType::Else));
    }
    {
        Lexer lex("if a {\n}\n// комментарий\n/* блочный */\nelse {\n}\n");
        auto tokens = lex.tokenize();
        CHECK(!semicolonBefore(tokens, TokenType::Else));
    }
    {
        Lexer lex("try {\n}\ncatch e: X {\n}\nfinally {\n}\n");
        auto tokens = lex.tokenize();
        CHECK(!semicolonBefore(tokens, TokenType::Catch));
        CHECK(!semicolonBefore(tokens, TokenType::Finally));
    }
    {
        // Идентификатор, лишь НАЧИНАЮЩИЙСЯ с else, — обычное слово: после '}' ';' вставляется как раньше.
        Lexer lex("if a {\n}\nelsewhere = 1\n");
        auto tokens = lex.tokenize();
        size_t brace = 0;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i].type == TokenType::RBrace) { brace = i; break; }
        }
        CHECK(brace + 1 < tokens.size() && tokens[brace + 1].type == TokenType::Semicolon);
    }
    {
        // Исключение действует только после '}': после выражения ';' по-прежнему вставляется.
        Lexer lex("x = 1\nelse");
        auto tokens = lex.tokenize();
        CHECK(semicolonBefore(tokens, TokenType::Else));
    }
}

void test_asi_after_closing_brace() {
    // '}' — ASI-триггер. Это может вставить ';' и там, где грамматика
    // formально её не требует (например, после блока if) — сознательно
    // компенсируется правилом "пустой statement" на стороне парсера,
    // а не усложнением лексера.
    Lexer lex("if x {\n"
               "    doSomething()\n"
               "}\n"
               "let done = true");
    auto tokens = lex.tokenize();
    CHECK(typeSeq(tokens).find("ERROR") == std::string::npos);
    CHECK(tokens.back().type == TokenType::EndOfFile);
}

void test_realistic_snippet_from_grammar_example() {
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
    Lexer lex(src);
    auto tokens = lex.tokenize();
    CHECK(typeSeq(tokens).find("ERROR") == std::string::npos);
    // ключевые точки: 'let radius: double' завершается ASI по 'double'
    bool foundLetDoubleSemi = false;
    for (size_t i = 0; i + 1 < tokens.size(); i++) {
        if (tokens[i].type == TokenType::KwDouble && tokens[i + 1].type == TokenType::Semicolon) {
            foundLetDoubleSemi = true;
        }
    }
    CHECK(foundLetDoubleSemi);
}

void test_error_token_on_unknown_char() {
    Lexer lex("let x = 5 @ 3");
    auto tokens = lex.tokenize();
    bool foundError = false;
    for (auto& t : tokens) if (t.type == TokenType::Error) foundError = true;
    CHECK(foundError);
}

} // namespace

int main() {
    test_keywords_and_identifiers();
    test_numbers();
    test_string_escapes();
    test_multichar_operators();
    test_introspection_tokens();
    test_comments_are_skipped();
    test_asi_inserts_after_identifier_and_literal();
    test_asi_no_insert_after_operator();
    test_asi_no_insert_after_comma_or_openparen();
    test_asi_multiline_call_needs_trailing_comma();
    test_asi_return_without_value_on_same_line();
    test_asi_after_closing_brace();
    test_asi_suppressed_before_else_catch_finally();
    test_realistic_snippet_from_grammar_example();
    test_error_token_on_unknown_char();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}

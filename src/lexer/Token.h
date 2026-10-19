#pragma once

#include <string>

namespace glass {

enum class TokenType {
    // Литералы и идентификаторы
    Identifier,
    IntLiteral,
    DoubleLiteral,
    StringLiteral,

    // Ключевые слова
    Mold, Facet, Fn, Let, Super, This,
    Static, Virtual, Override,
    Public, Private, Protected,
    If, Else, While, For, In,
    Return, Break, Continue,
    Try, Catch, Finally, Throw,
    True, False, Null,
    Layout, Vtable, Address, Heap, Gc,
    KwInt, KwDouble, KwBool, KwString,   // int, double, bool, string как типы

    // Операторы
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Eq, NotEq, Less, Greater, LessEq, GreaterEq,
    AndAnd, OrOr, Not,
    Arrow,      // ->
    DotDot,     // ..
    Tilde,      // ~
    Dot, Comma, Colon, Semicolon,

    // Скобки
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,

    // Служебные
    EndOfFile,
    Error,
};

// Человекочитаемое имя типа токена — для отладки и тестов
const char* tokenTypeName(TokenType type);

struct Token {
    TokenType type = TokenType::Error;
    std::string lexeme;     // сырой текст токена из исходника
    int line = 0;
    int column = 0;

    // Значения литералов — заполнено только соответствующее полю type
    long long intValue = 0;
    double doubleValue = 0.0;
    std::string stringValue; // для StringLiteral (после обработки escape) и Identifier
};

} // namespace glass

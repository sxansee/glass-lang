#pragma once

#include <string>
#include <vector>

#include "Token.h"

namespace glass {

// Лексер языка Glass.
//
// Помимо обычной токенизации, реализует автоматическую вставку ';' (ASI)
// по правилам, описанным в glass-grammar.md, раздел 1: если последний
// токен перед переводом строки принадлежит "завершающему" множеству
// (идентификатор, литерал, this/super/return/break/continue,
// примитивный тип, ) ] }), в поток токенов вставляется синтетический ';'.
//
// Исключение: после '}' ';' НЕ вставляется, если следующий токен —
// else, catch или finally (на этой же или любой из следующих строк).
// Так принятый в большинстве языков стиль
//     }
//     else {
// разбирается, а не ломается на синтетической ';'. Безопасно, потому что
// ни одна инструкция не может начинаться с else/catch/finally, а тело
// if/try — всегда блок в фигурных скобках.
//
// Упрощение относительно точного правила Go: перевод строки внутри
// блочного комментария тоже считается "увиденным переводом строки"
// для целей ASI, даже если в самом Go в этом случае возможны нюансы.
class Lexer {
public:
    explicit Lexer(std::string source);
    // startLine — номер строки, с которой начинается source. Для служебного
    // кода (prelude/Prelude.cpp) — НЕПОЛОЖИТЕЛЬНЫЙ: его строки не должны
    // совпадать со строками пользователя, иначе отладчик остановился бы
    // «внутри» Exception на чужой строке (см. VM::isUserLine).
    Lexer(std::string source, int startLine);

    // Полный проход по исходнику. Возвращает список токенов,
    // включая уже вставленные ASI-точки с запятой и завершающий EndOfFile.
    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;

    bool isAtEnd() const;
    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);

    // Пропускает пробелы/табы/переводы строк/комментарии.
    // Возвращает true, если по пути был пересечён хотя бы один перевод строки.
    bool skipWhitespaceAndComments();

    Token scanToken();
    Token scanIdentifierOrKeyword(int startLine, int startCol);
    Token scanNumber(int startLine, int startCol);
    Token scanString(int startLine, int startCol);
    Token makeToken(TokenType type, std::string lexeme, int line, int col) const;

    static bool triggersAsi(TokenType type);

    // true, если с текущей позиции (уже после пробелов и комментариев)
    // начинается слово else / catch / finally целиком (не префикс идентификатора).
    bool nextWordContinuesStatement() const;
};

} // namespace glass

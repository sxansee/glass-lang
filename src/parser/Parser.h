#pragma once

#include <optional>
#include <vector>

#include "../ast/Decl.h"
#include "../ast/Expr.h"
#include "../ast/Stmt.h"
#include "../lexer/Token.h"

namespace glass {

// Внутренний сигнал "не удалось разобрать текущую конструкцию" — ловится
// на границах деклараций/операторов, где вызывается synchronize().
struct ParseError {};

// Рекурсивный спуск по токенам из Lexer::tokenize() (раздел 2-7 grammar.md).
// Собирает все найденные ошибки в errors() вместо остановки на первой —
// после каждой ошибки вызывается synchronize() и разбор продолжается
// со следующей вероятной границы декларации/оператора.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parseProgram();
    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    std::vector<std::string> errors_;

    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool isAtEnd() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token& expect(TokenType type, const std::string& message);
    // Конец оператора: ';' (в том числе вставленная ASI) — либо, как в Go, ничего перед
    // закрывающей '}' или в конце файла: `if x { break }`, `{ return 1 }`, `mold A { let x: int }`.
    void expectStatementEnd(const std::string& message);
    void error(const Token& at, const std::string& message);
    void synchronize();
    static bool isOperandStartToken(TokenType t);

    // Верхний уровень
    TopLevelDecl parseTopLevelDecl();
    MoldDecl parseMoldDecl();
    FacetDecl parseFacetDecl();
    FunctionDecl parseFunctionDecl();

    std::vector<std::string> parseGenericParams();
    std::vector<std::string> parseBaseTypes();
    std::vector<Param> parseParamList();
    Param parseParam();
    std::optional<TypeNode> parseOptionalReturnType();
    AccessModifier parseOptionalAccessModifier();

    MoldMember parseMoldMember();
    FieldDecl parseFieldDecl(AccessModifier access, bool isStatic);
    MethodDecl parseMethodDecl(AccessModifier access, MethodModifier modifier);
    ConstructorDecl parseConstructorDecl(AccessModifier access);
    FacetMethodSig parseFacetMethodSig();

    TypeNode parseType();
    std::optional<TypeNode> tryParseCastType();
    std::optional<std::vector<TypeNode>> tryParseGenericArgs();

    // Операторы
    std::unique_ptr<BlockStmt> parseBlock();
    StmtPtr parseStatement();
    StmtPtr parseLetStatement();
    StmtPtr parseIfStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseForStatement();
    StmtPtr parseReturnStatement();
    StmtPtr parseTryStatement();

    // Выражения — precedence climbing, снизу вверх по разделу 7 грамматики
    ExprPtr parseExpression();
    ExprPtr parseAssignment();
    ExprPtr parseLogicalOr();
    ExprPtr parseLogicalAnd();
    ExprPtr parseEquality();
    ExprPtr parseRelational();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    std::vector<ExprPtr> parseArgList();
};

} // namespace glass

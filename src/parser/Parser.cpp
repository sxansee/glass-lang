#include "Parser.h"

#include <sstream>

namespace glass {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    if (offset < 0 || idx >= tokens_.size()) return tokens_.back();  // EOF всегда последний
    return tokens_[idx];
}

const Token& Parser::advance() {
    if (!isAtEnd()) pos_++;
    return tokens_[pos_ - 1];
}

void Parser::expectStatementEnd(const std::string& message) {
    if (match(TokenType::Semicolon)) return;
    if (check(TokenType::RBrace) || isAtEnd()) return;
    expect(TokenType::Semicolon, message);   // оформит обычную ошибку «ожидалась ';'»
}

bool Parser::isAtEnd() const { return peek().type == TokenType::EndOfFile; }

bool Parser::check(TokenType type) const { return peek().type == type; }

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

void Parser::error(const Token& at, const std::string& message) {
    std::ostringstream oss;
    oss << at.line << ":" << at.column << ": " << message
        << " (получено " << tokenTypeName(at.type) << ")";
    errors_.push_back(oss.str());
}

const Token& Parser::expect(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(peek(), message);
    throw ParseError{};
}

void Parser::synchronize() {
    // Всегда пропускаем хотя бы токен, на котором споткнулись — иначе,
    // если он сам оказался "точкой синхронизации" (например, 'facet' там,
    // где ожидалось имя конструктора), внешний цикл вызовет parseXxx()
    // на той же позиции и получит ту же ошибку снова: бесконечный цикл.
    if (!isAtEnd()) advance();
    while (!isAtEnd()) {
        if (pos_ > 0 && tokens_[pos_ - 1].type == TokenType::Semicolon) return;
        switch (peek().type) {
            case TokenType::Mold:
            case TokenType::Facet:
            case TokenType::Fn:
            case TokenType::Let:
            case TokenType::If:
            case TokenType::While:
            case TokenType::For:
            case TokenType::Return:
            case TokenType::Try:
                return;
            default:
                break;
        }
        advance();
    }
}

bool Parser::isOperandStartToken(TokenType t) {
    switch (t) {
        case TokenType::Identifier:
        case TokenType::IntLiteral:
        case TokenType::DoubleLiteral:
        case TokenType::StringLiteral:
        case TokenType::True:
        case TokenType::False:
        case TokenType::Null:
        case TokenType::This:
        case TokenType::Super:
        case TokenType::LParen:
        case TokenType::LBracket:
        case TokenType::Not:
        case TokenType::Minus:
        case TokenType::Tilde:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------
// Верхний уровень
// ---------------------------------------------------------------------

Program Parser::parseProgram() {
    Program program;
    while (!isAtEnd()) {
        if (match(TokenType::Semicolon)) continue;  // см. grammar.md §1: одиночная ';' — ASI после '}'
        try {
            program.declarations.push_back(parseTopLevelDecl());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    return program;
}

TopLevelDecl Parser::parseTopLevelDecl() {
    if (check(TokenType::Mold)) return parseMoldDecl();
    if (check(TokenType::Facet)) return parseFacetDecl();
    if (check(TokenType::Fn)) return parseFunctionDecl();
    error(peek(), "ожидалась декларация mold/facet/fn");
    advance();  // не застрять на месте
    throw ParseError{};
}

std::vector<std::string> Parser::parseGenericParams() {
    std::vector<std::string> params;
    if (!match(TokenType::Less)) return params;
    do {
        params.push_back(expect(TokenType::Identifier, "ожидался параметр обобщения").stringValue);
    } while (match(TokenType::Comma));
    expect(TokenType::Greater, "ожидалась '>'");
    return params;
}

std::vector<std::string> Parser::parseBaseTypes() {
    std::vector<std::string> bases;
    if (!match(TokenType::Colon)) return bases;
    do {
        bases.push_back(expect(TokenType::Identifier, "ожидалось имя базового типа").stringValue);
    } while (match(TokenType::Comma));
    return bases;
}

std::vector<Param> Parser::parseParamList() {
    std::vector<Param> params;
    if (check(TokenType::RParen)) return params;
    do {
        params.push_back(parseParam());
    } while (match(TokenType::Comma));
    return params;
}

Param Parser::parseParam() {
    Param p;
    const Token& name = expect(TokenType::Identifier, "ожидалось имя параметра");
    p.name = name.stringValue;
    p.line = name.line;
    p.column = name.column;
    expect(TokenType::Colon, "ожидалось ':' после имени параметра");
    p.type = parseType();
    return p;
}

std::optional<TypeNode> Parser::parseOptionalReturnType() {
    if (!match(TokenType::Arrow)) return std::nullopt;
    return parseType();
}

AccessModifier Parser::parseOptionalAccessModifier() {
    if (match(TokenType::Public)) return AccessModifier::Public;
    if (match(TokenType::Private)) return AccessModifier::Private;
    if (match(TokenType::Protected)) return AccessModifier::Protected;
    return AccessModifier::None;
}

MoldDecl Parser::parseMoldDecl() {
    MoldDecl decl;
    const Token& kw = expect(TokenType::Mold, "ожидалось 'mold'");
    decl.line = kw.line;
    decl.column = kw.column;
    decl.name = expect(TokenType::Identifier, "ожидалось имя mold'а").stringValue;
    decl.genericParams = parseGenericParams();
    decl.baseTypes = parseBaseTypes();
    expect(TokenType::LBrace, "ожидалась '{'");
    while (!check(TokenType::RBrace) && !isAtEnd()) {
        if (match(TokenType::Semicolon)) continue;
        try {
            decl.members.push_back(parseMoldMember());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    expect(TokenType::RBrace, "ожидалась '}'");
    return decl;
}

FacetDecl Parser::parseFacetDecl() {
    FacetDecl decl;
    const Token& kw = expect(TokenType::Facet, "ожидалось 'facet'");
    decl.line = kw.line;
    decl.column = kw.column;
    decl.name = expect(TokenType::Identifier, "ожидалось имя facet'а").stringValue;
    decl.genericParams = parseGenericParams();
    decl.baseTypes = parseBaseTypes();
    expect(TokenType::LBrace, "ожидалась '{'");
    while (!check(TokenType::RBrace) && !isAtEnd()) {
        if (match(TokenType::Semicolon)) continue;
        try {
            decl.methods.push_back(parseFacetMethodSig());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    expect(TokenType::RBrace, "ожидалась '}'");
    return decl;
}

FunctionDecl Parser::parseFunctionDecl() {
    FunctionDecl f;
    const Token& kw = expect(TokenType::Fn, "ожидалось 'fn'");
    f.line = kw.line;
    f.column = kw.column;
    f.name = expect(TokenType::Identifier, "ожидалось имя функции").stringValue;
    f.genericParams = parseGenericParams();
    expect(TokenType::LParen, "ожидалась '('");
    f.params = parseParamList();
    expect(TokenType::RParen, "ожидалась ')'");
    f.returnType = parseOptionalReturnType();
    f.body = parseBlock();
    return f;
}

MoldMember Parser::parseMoldMember() {
    AccessModifier access = parseOptionalAccessModifier();

    bool isStatic = false;
    MethodModifier methodMod = MethodModifier::None;
    if (match(TokenType::Static)) {
        isStatic = true;
        methodMod = MethodModifier::Static;
    } else if (match(TokenType::Virtual)) {
        methodMod = MethodModifier::Virtual;
    } else if (match(TokenType::Override)) {
        methodMod = MethodModifier::Override;
    }

    if (check(TokenType::Let)) return parseFieldDecl(access, isStatic);
    if (check(TokenType::Fn)) return parseMethodDecl(access, methodMod);
    return parseConstructorDecl(access);
}

FieldDecl Parser::parseFieldDecl(AccessModifier access, bool isStatic) {
    FieldDecl f;
    f.access = access;
    f.isStatic = isStatic;
    const Token& kw = expect(TokenType::Let, "ожидалось 'let'");
    f.line = kw.line;
    f.column = kw.column;
    f.name = expect(TokenType::Identifier, "ожидалось имя поля").stringValue;
    expect(TokenType::Colon, "ожидалось ':' после имени поля");
    f.type = parseType();
    if (match(TokenType::Assign)) {
        f.initExpr = parseExpression();
    }
    expectStatementEnd("ожидалась ';' после объявления поля");
    return f;
}

MethodDecl Parser::parseMethodDecl(AccessModifier access, MethodModifier modifier) {
    MethodDecl m;
    m.access = access;
    m.modifier = modifier;
    const Token& kw = expect(TokenType::Fn, "ожидалось 'fn'");
    m.line = kw.line;
    m.column = kw.column;
    m.name = expect(TokenType::Identifier, "ожидалось имя метода").stringValue;
    m.genericParams = parseGenericParams();
    expect(TokenType::LParen, "ожидалась '('");
    m.params = parseParamList();
    expect(TokenType::RParen, "ожидалась ')'");
    m.returnType = parseOptionalReturnType();
    m.body = parseBlock();
    return m;
}

ConstructorDecl Parser::parseConstructorDecl(AccessModifier access) {
    ConstructorDecl c;
    c.access = access;
    const Token& name = expect(TokenType::Identifier, "ожидалось имя конструктора (совпадающее с mold'ом)");
    c.name = name.stringValue;
    c.line = name.line;
    c.column = name.column;
    expect(TokenType::LParen, "ожидалась '('");
    c.params = parseParamList();
    expect(TokenType::RParen, "ожидалась ')'");
    c.body = parseBlock();
    return c;
}

FacetMethodSig Parser::parseFacetMethodSig() {
    FacetMethodSig sig;
    const Token& kw = expect(TokenType::Fn, "ожидалось 'fn'");
    sig.line = kw.line;
    sig.column = kw.column;
    sig.name = expect(TokenType::Identifier, "ожидалось имя метода").stringValue;
    expect(TokenType::LParen, "ожидалась '('");
    sig.params = parseParamList();
    expect(TokenType::RParen, "ожидалась ')'");
    sig.returnType = parseOptionalReturnType();
    expectStatementEnd("ожидалась ';' после сигнатуры метода facet'а");
    return sig;
}

// ---------------------------------------------------------------------
// Типы
// ---------------------------------------------------------------------

TypeNode Parser::parseType() {
    TypeNode t;
    t.line = peek().line;
    t.column = peek().column;

    if (match(TokenType::LBracket)) {
        t.kind = TypeNode::Kind::Array;
        t.elementType = std::make_shared<TypeNode>(parseType());
        expect(TokenType::RBracket, "ожидалась ']'");
        return t;
    }

    if (check(TokenType::KwInt) || check(TokenType::KwDouble) || check(TokenType::KwBool) ||
        check(TokenType::KwString)) {
        t.kind = TypeNode::Kind::Primitive;
        t.name = tokenTypeName(peek().type);
        advance();
        return t;
    }

    const Token& name = expect(TokenType::Identifier, "ожидалось имя типа");
    t.kind = TypeNode::Kind::Named;
    t.name = name.stringValue;
    if (match(TokenType::Less)) {
        do {
            t.genericArgs.push_back(parseType());
        } while (match(TokenType::Comma));
        expect(TokenType::Greater, "ожидалась '>'");
    }
    return t;
}

// Разбор "(" Type ")" вслепую с откатом — см. grammar.md §9, та же техника,
// что и для угловых скобок дженериков: жадная попытка, откат при неудаче.
std::optional<TypeNode> Parser::tryParseCastType() {
    if (!check(TokenType::LParen)) return std::nullopt;
    size_t savedPos = pos_;
    size_t savedErrCount = errors_.size();
    advance();  // '('
    try {
        TypeNode t = parseType();
        if (!check(TokenType::RParen)) throw ParseError{};
        advance();  // ')'
        if (!isOperandStartToken(peek().type)) throw ParseError{};
        return t;
    } catch (const ParseError&) {
        pos_ = savedPos;
        errors_.resize(savedErrCount);
        return std::nullopt;
    }
}

std::optional<std::vector<TypeNode>> Parser::tryParseGenericArgs() {
    if (!check(TokenType::Less)) return std::nullopt;
    size_t savedPos = pos_;
    size_t savedErrCount = errors_.size();
    advance();  // '<'
    try {
        std::vector<TypeNode> args;
        args.push_back(parseType());
        while (match(TokenType::Comma)) {
            args.push_back(parseType());
        }
        if (!check(TokenType::Greater)) throw ParseError{};
        advance();  // '>'
        return args;
    } catch (const ParseError&) {
        pos_ = savedPos;
        errors_.resize(savedErrCount);
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------
// Операторы
// ---------------------------------------------------------------------

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    auto block = std::make_unique<BlockStmt>();
    const Token& brace = expect(TokenType::LBrace, "ожидалась '{'");
    block->line = brace.line;
    block->column = brace.column;
    while (!check(TokenType::RBrace) && !isAtEnd()) {
        try {
            block->statements.push_back(parseStatement());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    expect(TokenType::RBrace, "ожидалась '}'");
    return block;
}

StmtPtr Parser::parseStatement() {
    if (check(TokenType::Semicolon)) {
        const Token& t = advance();
        auto s = std::make_unique<EmptyStmt>();
        s->line = t.line;
        s->column = t.column;
        return s;
    }
    if (check(TokenType::Let)) return parseLetStatement();
    if (check(TokenType::If)) return parseIfStatement();
    if (check(TokenType::While)) return parseWhileStatement();
    if (check(TokenType::For)) return parseForStatement();
    if (check(TokenType::Return)) return parseReturnStatement();
    if (check(TokenType::Break)) {
        const Token& t = advance();
        expectStatementEnd("ожидалась ';' после 'break'");
        auto s = std::make_unique<BreakStmt>();
        s->line = t.line;
        s->column = t.column;
        return s;
    }
    if (check(TokenType::Continue)) {
        const Token& t = advance();
        expectStatementEnd("ожидалась ';' после 'continue'");
        auto s = std::make_unique<ContinueStmt>();
        s->line = t.line;
        s->column = t.column;
        return s;
    }
    if (check(TokenType::Try)) return parseTryStatement();
    if (check(TokenType::Throw)) {
        const Token& t = advance();
        auto s = std::make_unique<ThrowStmt>();
        s->line = t.line;
        s->column = t.column;
        s->value = parseExpression();
        expectStatementEnd("ожидалась ';' после 'throw'");
        return s;
    }
    if (check(TokenType::LBrace)) return parseBlock();

    auto expr = parseExpression();
    expectStatementEnd("ожидалась ';' после выражения");
    auto s = std::make_unique<ExprStmt>();
    s->line = expr->line;
    s->column = expr->column;
    s->expr = std::move(expr);
    return s;
}

StmtPtr Parser::parseLetStatement() {
    auto s = std::make_unique<LetStmt>();
    const Token& kw = expect(TokenType::Let, "ожидалось 'let'");
    s->line = kw.line;
    s->column = kw.column;
    s->name = expect(TokenType::Identifier, "ожидалось имя переменной").stringValue;
    if (match(TokenType::Colon)) {
        s->type = parseType();
        if (match(TokenType::Assign)) {
            s->initExpr = parseExpression();
        }
    } else {
        expect(TokenType::Assign,
               "ожидалось '=' (без явного типа нужен инициализатор для вывода типа)");
        s->initExpr = parseExpression();
    }
    expectStatementEnd("ожидалась ';' после объявления переменной");
    return s;
}

StmtPtr Parser::parseIfStatement() {
    auto s = std::make_unique<IfStmt>();
    const Token& kw = expect(TokenType::If, "ожидалось 'if'");
    s->line = kw.line;
    s->column = kw.column;
    s->condition = parseExpression();
    s->thenBranch = parseBlock();
    if (match(TokenType::Else)) {
        if (check(TokenType::If)) {
            s->elseBranch = parseIfStatement();
        } else {
            s->elseBranch = parseBlock();
        }
    }
    return s;
}

StmtPtr Parser::parseWhileStatement() {
    auto s = std::make_unique<WhileStmt>();
    const Token& kw = expect(TokenType::While, "ожидалось 'while'");
    s->line = kw.line;
    s->column = kw.column;
    s->condition = parseExpression();
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseForStatement() {
    auto s = std::make_unique<ForStmt>();
    const Token& kw = expect(TokenType::For, "ожидалось 'for'");
    s->line = kw.line;
    s->column = kw.column;
    s->varName = expect(TokenType::Identifier, "ожидалось имя переменной цикла").stringValue;
    expect(TokenType::In, "ожидалось 'in'");
    s->iterable = parseExpression();
    if (match(TokenType::DotDot)) {
        s->rangeEnd = parseExpression();
    }
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseReturnStatement() {
    auto s = std::make_unique<ReturnStmt>();
    const Token& kw = expect(TokenType::Return, "ожидалось 'return'");
    s->line = kw.line;
    s->column = kw.column;
    if (!check(TokenType::Semicolon) && !check(TokenType::RBrace) && !isAtEnd()) {
        s->value = parseExpression();
    }
    expectStatementEnd("ожидалась ';' после 'return'");
    return s;
}

StmtPtr Parser::parseTryStatement() {
    auto s = std::make_unique<TryStmt>();
    const Token& kw = expect(TokenType::Try, "ожидалось 'try'");
    s->line = kw.line;
    s->column = kw.column;
    s->tryBlock = parseBlock();
    while (check(TokenType::Catch)) {
        advance();
        CatchClause c;
        c.varName = expect(TokenType::Identifier, "ожидалось имя переменной исключения").stringValue;
        expect(TokenType::Colon, "ожидалось ':' после имени переменной");
        c.type = parseType();
        c.body = parseBlock();
        s->catches.push_back(std::move(c));
    }
    if (match(TokenType::Finally)) {
        s->finallyBlock = parseBlock();
    }
    return s;
}

// ---------------------------------------------------------------------
// Выражения
// ---------------------------------------------------------------------

ExprPtr Parser::parseExpression() { return parseAssignment(); }

ExprPtr Parser::parseAssignment() {
    ExprPtr left = parseLogicalOr();
    if (check(TokenType::Assign) || check(TokenType::PlusAssign) || check(TokenType::MinusAssign) ||
        check(TokenType::StarAssign) || check(TokenType::SlashAssign)) {
        AssignOp op = AssignOp::Assign;
        switch (advance().type) {
            case TokenType::Assign: op = AssignOp::Assign; break;
            case TokenType::PlusAssign: op = AssignOp::AddAssign; break;
            case TokenType::MinusAssign: op = AssignOp::SubAssign; break;
            case TokenType::StarAssign: op = AssignOp::MulAssign; break;
            case TokenType::SlashAssign: op = AssignOp::DivAssign; break;
            default: break;
        }
        auto value = parseAssignment();  // право-ассоциативно
        auto e = std::make_unique<AssignExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = op;
        e->target = std::move(left);
        e->value = std::move(value);
        return e;
    }
    return left;
}

ExprPtr Parser::parseLogicalOr() {
    ExprPtr left = parseLogicalAnd();
    while (check(TokenType::OrOr)) {
        advance();
        auto right = parseLogicalAnd();
        auto e = std::make_unique<BinaryExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = BinaryOp::Or;
        e->left = std::move(left);
        e->right = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parseLogicalAnd() {
    ExprPtr left = parseEquality();
    while (check(TokenType::AndAnd)) {
        advance();
        auto right = parseEquality();
        auto e = std::make_unique<BinaryExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = BinaryOp::And;
        e->left = std::move(left);
        e->right = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parseEquality() {
    ExprPtr left = parseRelational();
    while (check(TokenType::Eq) || check(TokenType::NotEq)) {
        BinaryOp op = check(TokenType::Eq) ? BinaryOp::Eq : BinaryOp::NotEq;
        advance();
        auto right = parseRelational();
        auto e = std::make_unique<BinaryExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = op;
        e->left = std::move(left);
        e->right = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parseRelational() {
    ExprPtr left = parseAdditive();
    while (check(TokenType::Less) || check(TokenType::Greater) || check(TokenType::LessEq) ||
           check(TokenType::GreaterEq)) {
        BinaryOp op = BinaryOp::Less;
        switch (advance().type) {
            case TokenType::Less: op = BinaryOp::Less; break;
            case TokenType::Greater: op = BinaryOp::Greater; break;
            case TokenType::LessEq: op = BinaryOp::LessEq; break;
            case TokenType::GreaterEq: op = BinaryOp::GreaterEq; break;
            default: break;
        }
        auto right = parseAdditive();
        auto e = std::make_unique<BinaryExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = op;
        e->left = std::move(left);
        e->right = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        BinaryOp op = check(TokenType::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        advance();
        auto right = parseMultiplicative();
        auto e = std::make_unique<BinaryExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = op;
        e->left = std::move(left);
        e->right = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parseUnary();
    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        BinaryOp op = BinaryOp::Mul;
        switch (advance().type) {
            case TokenType::Star: op = BinaryOp::Mul; break;
            case TokenType::Slash: op = BinaryOp::Div; break;
            case TokenType::Percent: op = BinaryOp::Mod; break;
            default: break;
        }
        auto right = parseUnary();
        auto e = std::make_unique<BinaryExpr>();
        e->line = left->line;
        e->column = left->column;
        e->op = op;
        e->left = std::move(left);
        e->right = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parseUnary() {
    if (check(TokenType::Not) || check(TokenType::Minus)) {
        UnaryOp op = check(TokenType::Not) ? UnaryOp::Not : UnaryOp::Neg;
        const Token& opTok = advance();
        auto operand = parseUnary();
        auto e = std::make_unique<UnaryExpr>();
        e->line = opTok.line;
        e->column = opTok.column;
        e->op = op;
        e->operand = std::move(operand);
        return e;
    }

    if (check(TokenType::LParen)) {
        if (auto castType = tryParseCastType()) {
            auto operand = parseUnary();
            auto e = std::make_unique<CastExpr>();
            e->line = castType->line;
            e->column = castType->column;
            e->targetType = std::move(*castType);
            e->operand = std::move(operand);
            return e;
        }
    }

    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    while (true) {
        if (match(TokenType::Dot)) {
            std::string name = expect(TokenType::Identifier, "ожидалось имя после '.'").stringValue;
            auto access = std::make_unique<FieldAccessExpr>();
            access->line = expr->line;
            access->column = expr->column;
            access->name = name;
            access->target = std::move(expr);
            if (check(TokenType::LParen)) {
                advance();
                auto call = std::make_unique<CallExpr>();
                call->line = access->line;
                call->column = access->column;
                call->args = parseArgList();
                expect(TokenType::RParen, "ожидалась ')'");
                call->callee = std::move(access);
                expr = std::move(call);
            } else {
                expr = std::move(access);
            }
        } else if (match(TokenType::LBracket)) {
            // xs[i] — индекс; xs[a:b], xs[:b], xs[a:], xs[::k] — срез.
            ExprPtr first;
            if (!check(TokenType::Colon)) first = parseExpression();
            if (check(TokenType::Colon)) {
                auto slice = std::make_unique<SliceExpr>();
                slice->line = expr->line;
                slice->column = expr->column;
                slice->start = std::move(first);
                advance();  // первый ':'
                if (!check(TokenType::RBracket) && !check(TokenType::Colon)) slice->stop = parseExpression();
                if (match(TokenType::Colon)) {
                    if (!check(TokenType::RBracket)) slice->step = parseExpression();
                }
                expect(TokenType::RBracket, "ожидалась ']'");
                slice->target = std::move(expr);
                expr = std::move(slice);
            } else {
                auto idx = std::make_unique<IndexExpr>();
                idx->line = expr->line;
                idx->column = expr->column;
                idx->index = std::move(first);
                expect(TokenType::RBracket, "ожидалась ']'");
                idx->target = std::move(expr);
                expr = std::move(idx);
            }
        } else if (match(TokenType::LParen)) {
            auto call = std::make_unique<CallExpr>();
            call->line = expr->line;
            call->column = expr->column;
            call->args = parseArgList();
            expect(TokenType::RParen, "ожидалась ')'");
            call->callee = std::move(expr);
            expr = std::move(call);
        } else if (match(TokenType::Tilde)) {
            IntrospectKind kind;
            if (check(TokenType::Layout)) {
                kind = IntrospectKind::Layout;
            } else if (check(TokenType::Vtable)) {
                kind = IntrospectKind::Vtable;
            } else if (check(TokenType::Address)) {
                kind = IntrospectKind::Address;
            } else {
                error(peek(), "после '~' ожидалось layout/vtable/address");
                throw ParseError{};
            }
            advance();
            auto introspect = std::make_unique<IntrospectFieldExpr>();
            introspect->line = expr->line;
            introspect->column = expr->column;
            introspect->kind = kind;
            introspect->target = std::move(expr);
            expr = std::move(introspect);
        } else {
            break;
        }
    }
    return expr;
}

std::vector<ExprPtr> Parser::parseArgList() {
    std::vector<ExprPtr> args;
    if (check(TokenType::RParen)) return args;
    do {
        args.push_back(parseExpression());
    } while (match(TokenType::Comma));
    return args;
}

ExprPtr Parser::parsePrimary() {
    const Token& tok = peek();

    // int("42"), double(x), bool(s), string(n): имя типа, за которым сразу '(' — вызов встроенного
    // преобразования (запись (int) x — приведение — разбирается раньше, в parseUnary).
    if ((check(TokenType::KwInt) || check(TokenType::KwDouble) || check(TokenType::KwBool) ||
         check(TokenType::KwString)) && peek(1).type == TokenType::LParen) {
        advance();
        auto e = std::make_unique<IdentifierExpr>();
        e->line = tok.line;
        e->column = tok.column;
        e->name = tok.type == TokenType::KwInt ? "int" : tok.type == TokenType::KwDouble ? "double"
                : tok.type == TokenType::KwBool ? "bool" : "string";
        return e;
    }

    if (check(TokenType::IntLiteral)) {
        advance();
        auto e = std::make_unique<IntLiteralExpr>();
        e->line = tok.line;
        e->column = tok.column;
        e->value = tok.intValue;
        return e;
    }
    if (check(TokenType::DoubleLiteral)) {
        advance();
        auto e = std::make_unique<DoubleLiteralExpr>();
        e->line = tok.line;
        e->column = tok.column;
        e->value = tok.doubleValue;
        return e;
    }
    if (check(TokenType::True) || check(TokenType::False)) {
        advance();
        auto e = std::make_unique<BoolLiteralExpr>();
        e->line = tok.line;
        e->column = tok.column;
        e->value = (tok.type == TokenType::True);
        return e;
    }
    if (check(TokenType::StringLiteral)) {
        advance();
        auto e = std::make_unique<StringLiteralExpr>();
        e->line = tok.line;
        e->column = tok.column;
        e->value = tok.stringValue;
        return e;
    }
    if (check(TokenType::Null)) {
        advance();
        auto e = std::make_unique<NullLiteralExpr>();
        e->line = tok.line;
        e->column = tok.column;
        return e;
    }
    if (check(TokenType::This)) {
        advance();
        auto e = std::make_unique<ThisExpr>();
        e->line = tok.line;
        e->column = tok.column;
        return e;
    }
    if (check(TokenType::Super)) {
        advance();
        auto e = std::make_unique<SuperExpr>();
        e->line = tok.line;
        e->column = tok.column;
        if (match(TokenType::Dot)) {
            e->methodName = expect(TokenType::Identifier, "ожидалось имя метода после 'super.'").stringValue;
        }
        expect(TokenType::LParen, "ожидалась '(' после 'super'");
        e->args = parseArgList();
        expect(TokenType::RParen, "ожидалась ')'");
        return e;
    }
    if (check(TokenType::Identifier)) {
        advance();
        auto e = std::make_unique<IdentifierExpr>();
        e->line = tok.line;
        e->column = tok.column;
        e->name = tok.stringValue;
        if (auto args = tryParseGenericArgs()) {
            e->genericArgs = std::move(*args);
        }
        return e;
    }
    if (check(TokenType::LBracket)) {
        advance();
        auto e = std::make_unique<ArrayLiteralExpr>();
        e->line = tok.line;
        e->column = tok.column;
        if (!check(TokenType::RBracket)) {
            do {
                e->elements.push_back(parseExpression());
            } while (match(TokenType::Comma));
        }
        expect(TokenType::RBracket, "ожидалась ']'");
        return e;
    }
    if (check(TokenType::Tilde)) {
        advance();
        auto e = std::make_unique<GlobalIntrospectExpr>();
        e->line = tok.line;
        e->column = tok.column;
        if (check(TokenType::Heap)) {
            e->kind = GlobalIntrospectKind::Heap;
            advance();
        } else if (check(TokenType::Gc)) {
            e->kind = GlobalIntrospectKind::Gc;
            advance();
        } else {
            error(peek(), "после '~' ожидалось heap/gc/layout/vtable/address");
            throw ParseError{};
        }
        return e;
    }
    if (check(TokenType::LParen)) {
        advance();
        auto inner = parseExpression();
        expect(TokenType::RParen, "ожидалась ')'");
        return inner;
    }

    error(tok, "неожиданный токен в выражении");
    throw ParseError{};
}

} // namespace glass

#include "Lexer.h"

#include <cctype>
#include <cstring>
#include <unordered_map>

namespace glass {

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::Identifier: return "Identifier";
        case TokenType::IntLiteral: return "IntLiteral";
        case TokenType::DoubleLiteral: return "DoubleLiteral";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::Mold: return "mold";
        case TokenType::Facet: return "facet";
        case TokenType::Fn: return "fn";
        case TokenType::Let: return "let";
        case TokenType::Super: return "super";
        case TokenType::This: return "this";
        case TokenType::Static: return "static";
        case TokenType::Virtual: return "virtual";
        case TokenType::Override: return "override";
        case TokenType::Public: return "public";
        case TokenType::Private: return "private";
        case TokenType::Protected: return "protected";
        case TokenType::If: return "if";
        case TokenType::Else: return "else";
        case TokenType::While: return "while";
        case TokenType::For: return "for";
        case TokenType::In: return "in";
        case TokenType::Return: return "return";
        case TokenType::Break: return "break";
        case TokenType::Continue: return "continue";
        case TokenType::Try: return "try";
        case TokenType::Catch: return "catch";
        case TokenType::Finally: return "finally";
        case TokenType::Throw: return "throw";
        case TokenType::True: return "true";
        case TokenType::False: return "false";
        case TokenType::Null: return "null";
        case TokenType::Layout: return "layout";
        case TokenType::Vtable: return "vtable";
        case TokenType::Address: return "address";
        case TokenType::Heap: return "heap";
        case TokenType::Gc: return "gc";
        case TokenType::KwInt: return "int";
        case TokenType::KwDouble: return "double";
        case TokenType::KwBool: return "bool";
        case TokenType::KwString: return "string";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::Assign: return "=";
        case TokenType::PlusAssign: return "+=";
        case TokenType::MinusAssign: return "-=";
        case TokenType::StarAssign: return "*=";
        case TokenType::SlashAssign: return "/=";
        case TokenType::Eq: return "==";
        case TokenType::NotEq: return "!=";
        case TokenType::Less: return "<";
        case TokenType::Greater: return ">";
        case TokenType::LessEq: return "<=";
        case TokenType::GreaterEq: return ">=";
        case TokenType::AndAnd: return "&&";
        case TokenType::OrOr: return "||";
        case TokenType::Not: return "!";
        case TokenType::Arrow: return "->";
        case TokenType::DotDot: return "..";
        case TokenType::Tilde: return "~";
        case TokenType::Dot: return ".";
        case TokenType::Comma: return ",";
        case TokenType::Colon: return ":";
        case TokenType::Semicolon: return ";";
        case TokenType::LParen: return "(";
        case TokenType::RParen: return ")";
        case TokenType::LBrace: return "{";
        case TokenType::RBrace: return "}";
        case TokenType::LBracket: return "[";
        case TokenType::RBracket: return "]";
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Error: return "ERROR";
    }
    return "UNKNOWN";
}

namespace {
const std::unordered_map<std::string, TokenType>& keywordTable() {
    static const std::unordered_map<std::string, TokenType> table = {
        {"mold", TokenType::Mold}, {"facet", TokenType::Facet}, {"fn", TokenType::Fn},
        {"let", TokenType::Let}, {"super", TokenType::Super}, {"this", TokenType::This},
        {"static", TokenType::Static}, {"virtual", TokenType::Virtual}, {"override", TokenType::Override},
        {"public", TokenType::Public}, {"private", TokenType::Private}, {"protected", TokenType::Protected},
        {"if", TokenType::If}, {"else", TokenType::Else}, {"while", TokenType::While},
        {"for", TokenType::For}, {"in", TokenType::In},
        {"return", TokenType::Return}, {"break", TokenType::Break}, {"continue", TokenType::Continue},
        {"try", TokenType::Try}, {"catch", TokenType::Catch}, {"finally", TokenType::Finally}, {"throw", TokenType::Throw},
        {"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
        {"layout", TokenType::Layout}, {"vtable", TokenType::Vtable}, {"address", TokenType::Address},
        {"heap", TokenType::Heap}, {"gc", TokenType::Gc},
        {"int", TokenType::KwInt}, {"double", TokenType::KwDouble},
        {"bool", TokenType::KwBool}, {"string", TokenType::KwString},
    };
    return table;
}
} // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

Lexer::Lexer(std::string source, int startLine) : source_(std::move(source)), line_(startLine) {}

bool Lexer::isAtEnd() const { return pos_ >= source_.size(); }

char Lexer::peek(int offset) const {
    size_t p = pos_ + static_cast<size_t>(offset);
    if (offset < 0 || p >= source_.size()) return '\0';
    return source_[p];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[pos_] != expected) return false;
    advance();
    return true;
}

bool Lexer::skipWhitespaceAndComments() {
    bool sawNewline = false;
    while (!isAtEnd()) {
        char c = peek();
        if (c == '\n') {
            sawNewline = true;
            advance();
        } else if (c == ' ' || c == '\t' || c == '\r') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (!isAtEnd() && peek() != '\n') advance();
            // сам перевод строки после // подхватит следующая итерация цикла
        } else if (c == '/' && peek(1) == '*') {
            advance();
            advance();
            while (!isAtEnd() && !(peek() == '*' && peek(1) == '/')) {
                if (peek() == '\n') sawNewline = true;
                advance();
            }
            if (!isAtEnd()) {
                advance();
                advance();
            }
        } else {
            break;
        }
    }
    return sawNewline;
}

bool Lexer::triggersAsi(TokenType t) {
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
        case TokenType::Return:
        case TokenType::Break:
        case TokenType::Continue:
        case TokenType::KwInt:
        case TokenType::KwDouble:
        case TokenType::KwBool:
        case TokenType::KwString:
        case TokenType::Layout:
        case TokenType::Vtable:
        case TokenType::Address:
        case TokenType::Heap:
        case TokenType::Gc:
        case TokenType::RParen:
        case TokenType::RBracket:
        case TokenType::RBrace:
            return true;
        default:
            return false;
    }
}

bool Lexer::nextWordContinuesStatement() const {
    static const char* const kWords[] = {"else", "catch", "finally"};
    for (const char* word : kWords) {
        size_t n = std::strlen(word);
        if (pos_ + n > source_.size()) continue;
        if (source_.compare(pos_, n, word) != 0) continue;
        size_t after = pos_ + n;
        if (after >= source_.size()) return true;
        unsigned char c = static_cast<unsigned char>(source_[after]);
        if (!std::isalnum(c) && c != '_') return true;  // слово целиком, а не начало идентификатора else1
    }
    return false;
}

Token Lexer::makeToken(TokenType type, std::string lexeme, int line, int col) const {
    Token t;
    t.type = type;
    t.lexeme = std::move(lexeme);
    t.line = line;
    t.column = col;
    return t;
}

Token Lexer::scanIdentifierOrKeyword(int startLine, int startCol) {
    size_t start = pos_ - 1;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        advance();
    }
    std::string text = source_.substr(start, pos_ - start);

    const auto& table = keywordTable();
    auto it = table.find(text);
    TokenType type = (it != table.end()) ? it->second : TokenType::Identifier;

    Token tok = makeToken(type, text, startLine, startCol);
    if (type == TokenType::Identifier) tok.stringValue = text;
    return tok;
}

Token Lexer::scanNumber(int startLine, int startCol) {
    size_t start = pos_ - 1;
    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();

    bool isDouble = false;
    if (!isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        isDouble = true;
        advance(); // '.'
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
        size_t savePos = pos_;
        int saveLine = line_;
        int saveCol = column_;
        advance(); // e/E
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) advance();
        if (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            isDouble = true;
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        } else {
            // это была не экспонента — откатываемся
            pos_ = savePos;
            line_ = saveLine;
            column_ = saveCol;
        }
    }

    std::string text = source_.substr(start, pos_ - start);
    Token tok = makeToken(isDouble ? TokenType::DoubleLiteral : TokenType::IntLiteral,
                           text, startLine, startCol);
    if (isDouble) {
        tok.doubleValue = std::stod(text);
    } else {
        tok.intValue = std::stoll(text);
    }
    return tok;
}

Token Lexer::scanString(int startLine, int startCol) {
    size_t lexemeStart = pos_ - 1; // включая открывающую "
    std::string value;
    while (!isAtEnd() && peek() != '"' && peek() != '\n') {
        char c = advance();
        if (c == '\\' && !isAtEnd()) {
            char esc = advance();
            switch (esc) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                default: value += esc; break;
            }
        } else {
            value += c;
        }
    }

    if (isAtEnd() || peek() == '\n') {
        // незакрытая строка
        std::string lexeme = source_.substr(lexemeStart, pos_ - lexemeStart);
        return makeToken(TokenType::Error, lexeme, startLine, startCol);
    }

    advance(); // закрывающая "
    std::string lexeme = source_.substr(lexemeStart, pos_ - lexemeStart);
    Token tok = makeToken(TokenType::StringLiteral, lexeme, startLine, startCol);
    tok.stringValue = value;
    return tok;
}

Token Lexer::scanToken() {
    int startLine = line_;
    int startCol = column_;
    char c = advance();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return scanIdentifierOrKeyword(startLine, startCol);
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scanNumber(startLine, startCol);
    }
    if (c == '"') {
        return scanString(startLine, startCol);
    }

    switch (c) {
        case '+':
            return match('=') ? makeToken(TokenType::PlusAssign, "+=", startLine, startCol)
                               : makeToken(TokenType::Plus, "+", startLine, startCol);
        case '-':
            if (match('=')) return makeToken(TokenType::MinusAssign, "-=", startLine, startCol);
            if (match('>')) return makeToken(TokenType::Arrow, "->", startLine, startCol);
            return makeToken(TokenType::Minus, "-", startLine, startCol);
        case '*':
            return match('=') ? makeToken(TokenType::StarAssign, "*=", startLine, startCol)
                               : makeToken(TokenType::Star, "*", startLine, startCol);
        case '/':
            return match('=') ? makeToken(TokenType::SlashAssign, "/=", startLine, startCol)
                               : makeToken(TokenType::Slash, "/", startLine, startCol);
        case '%':
            return makeToken(TokenType::Percent, "%", startLine, startCol);
        case '=':
            return match('=') ? makeToken(TokenType::Eq, "==", startLine, startCol)
                               : makeToken(TokenType::Assign, "=", startLine, startCol);
        case '!':
            return match('=') ? makeToken(TokenType::NotEq, "!=", startLine, startCol)
                               : makeToken(TokenType::Not, "!", startLine, startCol);
        case '<':
            return match('=') ? makeToken(TokenType::LessEq, "<=", startLine, startCol)
                               : makeToken(TokenType::Less, "<", startLine, startCol);
        case '>':
            return match('=') ? makeToken(TokenType::GreaterEq, ">=", startLine, startCol)
                               : makeToken(TokenType::Greater, ">", startLine, startCol);
        case '&':
            if (match('&')) return makeToken(TokenType::AndAnd, "&&", startLine, startCol);
            return makeToken(TokenType::Error, "&", startLine, startCol);
        case '|':
            if (match('|')) return makeToken(TokenType::OrOr, "||", startLine, startCol);
            return makeToken(TokenType::Error, "|", startLine, startCol);
        case '~':
            return makeToken(TokenType::Tilde, "~", startLine, startCol);
        case '.':
            if (match('.')) return makeToken(TokenType::DotDot, "..", startLine, startCol);
            return makeToken(TokenType::Dot, ".", startLine, startCol);
        case ',': return makeToken(TokenType::Comma, ",", startLine, startCol);
        case ':': return makeToken(TokenType::Colon, ":", startLine, startCol);
        case ';': return makeToken(TokenType::Semicolon, ";", startLine, startCol);
        case '(': return makeToken(TokenType::LParen, "(", startLine, startCol);
        case ')': return makeToken(TokenType::RParen, ")", startLine, startCol);
        case '{': return makeToken(TokenType::LBrace, "{", startLine, startCol);
        case '}': return makeToken(TokenType::RBrace, "}", startLine, startCol);
        case '[': return makeToken(TokenType::LBracket, "[", startLine, startCol);
        case ']': return makeToken(TokenType::RBracket, "]", startLine, startCol);
        default:
            return makeToken(TokenType::Error, std::string(1, c), startLine, startCol);
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    TokenType lastType = TokenType::Error;
    bool hasLast = false;

    while (true) {
        bool sawNewline = skipWhitespaceAndComments();

        // После '}' перед else/catch/finally ';' не вставляется (см. Lexer.h):
        // стиль с else на новой строке — тот же if/else, а не пустой оператор.
        bool continuesStatement = hasLast && lastType == TokenType::RBrace && nextWordContinuesStatement();
        if (sawNewline && hasLast && triggersAsi(lastType) && !continuesStatement) {
            tokens.push_back(makeToken(TokenType::Semicolon, ";", line_, column_));
            hasLast = false; // синтетическая ';' сама не должна триггерить ещё одну ASI
        }

        if (isAtEnd()) {
            if (hasLast && triggersAsi(lastType)) {
                tokens.push_back(makeToken(TokenType::Semicolon, ";", line_, column_));
            }
            tokens.push_back(makeToken(TokenType::EndOfFile, "", line_, column_));
            break;
        }

        Token tok = scanToken();
        tokens.push_back(tok);
        lastType = tok.type;
        hasLast = true;
    }

    return tokens;
}

} // namespace glass

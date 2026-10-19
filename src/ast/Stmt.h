#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Expr.h"
#include "Type.h"

namespace glass {

struct LetStmt;
struct IfStmt;
struct WhileStmt;
struct ForStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct TryStmt;
struct ThrowStmt;
struct ExprStmt;
struct BlockStmt;
struct EmptyStmt;

struct StmtVisitor {
    virtual ~StmtVisitor() = default;
    virtual void visit(LetStmt&) = 0;
    virtual void visit(IfStmt&) = 0;
    virtual void visit(WhileStmt&) = 0;
    virtual void visit(ForStmt&) = 0;
    virtual void visit(ReturnStmt&) = 0;
    virtual void visit(BreakStmt&) = 0;
    virtual void visit(ContinueStmt&) = 0;
    virtual void visit(TryStmt&) = 0;
    virtual void visit(ThrowStmt&) = 0;
    virtual void visit(ExprStmt&) = 0;
    virtual void visit(BlockStmt&) = 0;
    virtual void visit(EmptyStmt&) = 0;
};

struct Stmt {
    int line = 0;
    int column = 0;
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor& v) = 0;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct LetStmt : Stmt {
    std::string name;
    std::optional<TypeNode> type;  // nullopt только когда тип выводится из initExpr
    ExprPtr initExpr;              // nullptr допустим лишь при явном type без инициализатора
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> statements;
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct IfStmt : Stmt {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> thenBranch;
    StmtPtr elseBranch;  // nullable; BlockStmt или вложенный IfStmt (else if)
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> body;
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct ForStmt : Stmt {
    std::string varName;
    ExprPtr iterable;
    ExprPtr rangeEnd;  // не nullptr только для формы "for x in a..b"
    std::unique_ptr<BlockStmt> body;
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct ReturnStmt : Stmt {
    ExprPtr value;  // nullable
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct BreakStmt : Stmt {
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct ContinueStmt : Stmt {
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct CatchClause {
    std::string varName;
    TypeNode type;
    std::unique_ptr<BlockStmt> body;
};

struct TryStmt : Stmt {
    std::unique_ptr<BlockStmt> tryBlock;
    std::vector<CatchClause> catches;
    std::unique_ptr<BlockStmt> finallyBlock;  // nullable
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct ThrowStmt : Stmt {
    ExprPtr value;
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

// Одиночная ';' — легальный "пустой" оператор. Нужен потому, что ASI
// вставляет ';' после каждой '}', в том числе там, где грамматика этого
// формально не требует (glass-grammar.md, раздел 1 и 6).
struct EmptyStmt : Stmt {
    void accept(StmtVisitor& v) override { v.visit(*this); }
};

} // namespace glass

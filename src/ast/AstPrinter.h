#pragma once

#include <string>

#include "Decl.h"
#include "Expr.h"
#include "Stmt.h"

namespace glass {

// Печатает AST в компактном S-expression-подобном виде, например:
//   (binary + (int 3) (int 4))
// Формат не претендует на то, чтобы быть валидным Glass-кодом — это
// инструмент для тестов (сравнение строк) и отладки, а не pretty-printer.
class AstPrinter : public ExprVisitor, public StmtVisitor {
public:
    std::string print(Expr& e);
    std::string print(Stmt& s);

    void visit(IntLiteralExpr&) override;
    void visit(DoubleLiteralExpr&) override;
    void visit(BoolLiteralExpr&) override;
    void visit(StringLiteralExpr&) override;
    void visit(NullLiteralExpr&) override;
    void visit(ThisExpr&) override;
    void visit(SuperExpr&) override;
    void visit(IdentifierExpr&) override;
    void visit(ArrayLiteralExpr&) override;
    void visit(GlobalIntrospectExpr&) override;
    void visit(UnaryExpr&) override;
    void visit(CastExpr&) override;
    void visit(BinaryExpr&) override;
    void visit(AssignExpr&) override;
    void visit(CallExpr&) override;
    void visit(FieldAccessExpr&) override;
    void visit(IndexExpr&) override;
    void visit(SliceExpr&) override;
    void visit(IntrospectFieldExpr&) override;

    void visit(LetStmt&) override;
    void visit(IfStmt&) override;
    void visit(WhileStmt&) override;
    void visit(ForStmt&) override;
    void visit(ReturnStmt&) override;
    void visit(BreakStmt&) override;
    void visit(ContinueStmt&) override;
    void visit(TryStmt&) override;
    void visit(ThrowStmt&) override;
    void visit(ExprStmt&) override;
    void visit(BlockStmt&) override;
    void visit(EmptyStmt&) override;

private:
    std::string out_;
};

std::string printMoldMember(MoldMember& member);
std::string printTopLevelDecl(TopLevelDecl& decl);
std::string printProgram(Program& program);

} // namespace glass

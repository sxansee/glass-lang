#pragma once
// Размеры всех узлов AST для сверки раскладки между единицами трансляции (см. util/LayoutCheck.h).
// Добавили новый узел или структуру в Type.h/Expr.h/Stmt.h/Decl.h — добавьте её и сюда.

#include "Decl.h"
#include "Expr.h"
#include "Stmt.h"
#include "Type.h"
#include "../util/LayoutCheck.h"

#define GLASS_AST_LAYOUT_VALUES                                                                                         \
    sizeof(::glass::TypeNode), sizeof(::glass::Expr), sizeof(::glass::IntLiteralExpr), sizeof(::glass::DoubleLiteralExpr), \
    sizeof(::glass::BoolLiteralExpr), sizeof(::glass::StringLiteralExpr), sizeof(::glass::NullLiteralExpr),            \
    sizeof(::glass::ThisExpr), sizeof(::glass::SuperExpr), sizeof(::glass::IdentifierExpr),                            \
    sizeof(::glass::ArrayLiteralExpr), sizeof(::glass::GlobalIntrospectExpr), sizeof(::glass::UnaryExpr),              \
    sizeof(::glass::CastExpr), sizeof(::glass::BinaryExpr), sizeof(::glass::AssignExpr), sizeof(::glass::CallExpr),    \
    sizeof(::glass::FieldAccessExpr), sizeof(::glass::IndexExpr), sizeof(::glass::SliceExpr),                          \
    sizeof(::glass::IntrospectFieldExpr), sizeof(::glass::Stmt), sizeof(::glass::LetStmt), sizeof(::glass::BlockStmt), \
    sizeof(::glass::IfStmt), sizeof(::glass::WhileStmt), sizeof(::glass::ForStmt), sizeof(::glass::ReturnStmt),        \
    sizeof(::glass::BreakStmt), sizeof(::glass::ContinueStmt), sizeof(::glass::CatchClause), sizeof(::glass::TryStmt), \
    sizeof(::glass::ThrowStmt), sizeof(::glass::ExprStmt), sizeof(::glass::EmptyStmt), sizeof(::glass::Param),         \
    sizeof(::glass::FieldDecl), sizeof(::glass::MethodDecl), sizeof(::glass::ConstructorDecl),                         \
    sizeof(::glass::MoldDecl), sizeof(::glass::FacetMethodSig), sizeof(::glass::FacetDecl),                            \
    sizeof(::glass::FunctionDecl), sizeof(::glass::Program)

// Определяет в текущей единице трансляции функцию-отпечаток, например GLASS_DEFINE_AST_LAYOUT(parserAstLayout).
#define GLASS_DEFINE_AST_LAYOUT(functionName) \
    unsigned long long glass::functionName() { return ::glass::layoutOf({GLASS_AST_LAYOUT_VALUES}); }

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Type.h"

namespace glass {

struct IntLiteralExpr;
struct DoubleLiteralExpr;
struct BoolLiteralExpr;
struct StringLiteralExpr;
struct NullLiteralExpr;
struct ThisExpr;
struct SuperExpr;
struct IdentifierExpr;
struct ArrayLiteralExpr;
struct GlobalIntrospectExpr;
struct UnaryExpr;
struct CastExpr;
struct BinaryExpr;
struct AssignExpr;
struct CallExpr;
struct FieldAccessExpr;
struct IndexExpr;
struct SliceExpr;
struct IntrospectFieldExpr;

struct ExprVisitor {
    virtual ~ExprVisitor() = default;
    virtual void visit(IntLiteralExpr&) = 0;
    virtual void visit(DoubleLiteralExpr&) = 0;
    virtual void visit(BoolLiteralExpr&) = 0;
    virtual void visit(StringLiteralExpr&) = 0;
    virtual void visit(NullLiteralExpr&) = 0;
    virtual void visit(ThisExpr&) = 0;
    virtual void visit(SuperExpr&) = 0;
    virtual void visit(IdentifierExpr&) = 0;
    virtual void visit(ArrayLiteralExpr&) = 0;
    virtual void visit(GlobalIntrospectExpr&) = 0;
    virtual void visit(UnaryExpr&) = 0;
    virtual void visit(CastExpr&) = 0;
    virtual void visit(BinaryExpr&) = 0;
    virtual void visit(AssignExpr&) = 0;
    virtual void visit(CallExpr&) = 0;
    virtual void visit(FieldAccessExpr&) = 0;
    virtual void visit(IndexExpr&) = 0;
    virtual void visit(SliceExpr&) = 0;
    virtual void visit(IntrospectFieldExpr&) = 0;
};

struct Expr {
    int line = 0;
    int column = 0;
    // Заполняется BodyChecker'ом при успешной проверке (см. sema/BodyChecker.cpp).
    // Кодогенерации не нужно заново выводить типы — она читает уже готовое
    // значение отсюда (нужно, например, чтобы найти смещение поля при
    // компиляции obj.field, или решить virtual/статический вызов метода).
    TypeNode resolvedType;
    // Неявное расширение int → double: BodyChecker ставит флаг, когда принимает int-выражение там,
    // где ждут double (let d: double = 5; передача аргумента; return; элемент массива). Кодогенерация
    // после вычисления выражения добавляет преобразование — иначе в double-переменной лежало бы
    // целое, и d / 2 считалось бы как целочисленное деление.
    bool convertToDouble = false;
    virtual ~Expr() = default;
    virtual void accept(ExprVisitor& v) = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr : Expr {
    long long value = 0;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct DoubleLiteralExpr : Expr {
    double value = 0.0;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct BoolLiteralExpr : Expr {
    bool value = false;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct StringLiteralExpr : Expr {
    std::string value;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct NullLiteralExpr : Expr {
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct ThisExpr : Expr {
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

// super(args) — цепочка к конструктору базового mold'а (methodName пуст),
// либо super.method(args) — вызов метода базового mold'а.
struct SuperExpr : Expr {
    std::string methodName;
    std::vector<ExprPtr> args;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct IdentifierExpr : Expr {
    std::string name;
    std::vector<TypeNode> genericArgs;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct ArrayLiteralExpr : Expr {
    std::vector<ExprPtr> elements;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

enum class GlobalIntrospectKind { Heap, Gc };

struct GlobalIntrospectExpr : Expr {
    GlobalIntrospectKind kind = GlobalIntrospectKind::Heap;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

enum class UnaryOp { Not, Neg };

struct UnaryExpr : Expr {
    UnaryOp op = UnaryOp::Neg;
    ExprPtr operand;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct CastExpr : Expr {
    TypeNode targetType;
    ExprPtr operand;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    Eq, NotEq, Less, Greater, LessEq, GreaterEq,
    And, Or,
};

struct BinaryExpr : Expr {
    BinaryOp op = BinaryOp::Add;
    ExprPtr left;
    ExprPtr right;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

enum class AssignOp { Assign, AddAssign, SubAssign, MulAssign, DivAssign };

struct AssignExpr : Expr {
    AssignOp op = AssignOp::Assign;
    ExprPtr target;
    ExprPtr value;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

// Используется и для вызова функции/метода, и (без 'new' в языке) для
// создания объекта — какой из двух случаев это на самом деле, решает
// семантический анализ по таблице символов, не парсер.
struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct FieldAccessExpr : Expr {
    ExprPtr target;
    std::string name;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

struct IndexExpr : Expr {
    ExprPtr target;
    ExprPtr index;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

// xs[a:b], xs[:b], xs[a:], xs[::2], s[1:-1] — срез массива или строки (как в Python). Любая из
// трёх частей может отсутствовать (nullptr).
struct SliceExpr : Expr {
    ExprPtr target;
    ExprPtr start;
    ExprPtr stop;
    ExprPtr step;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

enum class IntrospectKind { Layout, Vtable, Address };

struct IntrospectFieldExpr : Expr {
    ExprPtr target;
    IntrospectKind kind = IntrospectKind::Layout;
    void accept(ExprVisitor& v) override { v.visit(*this); }
};

std::string binaryOpToString(BinaryOp op);
std::string assignOpToString(AssignOp op);
std::string unaryOpToString(UnaryOp op);
std::string introspectKindToString(IntrospectKind kind);

} // namespace glass

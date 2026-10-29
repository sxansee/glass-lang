#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ast/Decl.h"
#include "../bytecode/Builtins.h"
#include "../ast/Expr.h"
#include "../ast/Stmt.h"
#include "Sema.h"

namespace glass {

// Тайпчекер тел функций/методов/конструкторов — вторая половина
// семантического анализа, работающая поверх уже построенной и проверенной
// Sema таблицы символов (декларации, наследование, facet-контракты).
//
// Сознательные упрощения (см. README «Что дальше»):
//  - типы в catch-секциях (например 'Exception') не проверяются на
//    существование — стандартная библиотека исключений ещё не
//    спроектирована; такие типы считаются "непрозрачными": операции над
//    ними (доступ к полям/методам) не проверяются и не считаются ошибкой;
//  - дженерики проверяются без подстановки: параметр обобщения совместим
//    с любым типом (стирание типов, без полной проверки инстанциаций);
//  - 'print' — единственная встроенная функция, известная тайпчекеру
//    (принимает один аргумент любого типа, ничего не возвращает); других
//    функций стандартной библиотеки (List<T>, Canvas, ...) он не знает.
class BodyChecker : public ExprVisitor, public StmtVisitor {
public:
    explicit BodyChecker(const Sema& sema);

    std::vector<std::string> checkProgram(Program& program);

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
    const Sema& sema_;
    std::vector<std::string> errors_;
    TypeNode lastType_;

    std::string currentMoldName_;  // пусто — вне метода/конструктора mold'а
    std::optional<TypeNode> currentReturnType_;
    std::vector<std::string> currentGenericScope_;
    int loopDepth_ = 0;

    struct Scope {
        std::unordered_map<std::string, TypeNode> vars;
        Scope* parent = nullptr;
    };
    std::vector<std::unique_ptr<Scope>> scopeStack_;

    void pushScope();
    void popScope();
    void defineVar(const std::string& name, const TypeNode& type, int line, int col);
    std::optional<TypeNode> lookupVar(const std::string& name) const;

    void error(int line, int col, const std::string& message);
    TypeNode checkExpr(Expr& e);
    TypeNode checkExprAgainstExpected(Expr& e, const TypeNode& expected);
    void checkStmt(Stmt& s);
    // Функция или метод с '-> тип' обязаны вернуть значение на каждом пути выполнения: иначе на
    // «пропущенной» ветке вернулся бы null. what — «функции 'f'» / «метода 'm' mold'а 'M'» для сообщения.
    void checkAllPathsReturn(const std::string& what, const std::optional<TypeNode>& returnType, const BlockStmt& body,
                             int line, int col);

    bool isAssignable(const TypeNode& from, const TypeNode& to) const;
    bool isGenericParam(const std::string& name) const;
    bool isKnownType(const TypeNode& t) const;
    bool moldImplementsFacet(const std::string& moldName, const std::string& facetName) const;
    bool facetExtendsOrIs(const std::string& facetName, const std::string& target) const;

    // Ближайшее объявление по цепочке mold'ов startMold → его базы. owner получает имя mold'а, в котором
    // член объявлен (нужно для проверки доступа).
    const MethodDecl* findMethod(const std::string& startMold, const std::string& methodName,
                                 std::string* owner = nullptr) const;
    const FieldDecl* findField(const std::string& startMold, const std::string& fieldName,
                               std::string* owner = nullptr) const;
    const FacetMethodSig* findFacetMethodSig(const std::string& facetName, const std::string& methodName) const;

    // Модификаторы доступа (как в C#): private — только код самого mold'а-владельца (в том числе через
    // другой его экземпляр); protected — владелец и его потомки; public и отсутствие модификатора — везде.
    // Свободная функция видит только открытое. Проверка идёт по СТАТИЧЕСКОМУ типу; у protected не
    // проверяется тип получателя (в C# через ссылку на базовый тип protected-член недоступен — здесь доступен).
    enum class MemberKind { Field, Method, Constructor };
    bool isMemberAccessible(AccessModifier access, const std::string& owner) const;
    // Сообщает об ошибке, если к члену нельзя обратиться из текущего места; true — доступ разрешён.
    // Для конструктора owner — имя mold'а, member не используется.
    bool checkMemberAccess(AccessModifier access, const std::string& owner, const std::string& member, MemberKind kind,
                           int line, int col);

    void checkArgsAgainstParams(const std::vector<Param>& params, CallExpr& call);
    TypeNode checkFunctionCall(const FunctionDecl& fn, CallExpr& call);
    TypeNode checkConstruction(const MoldDecl& mold, CallExpr& call);
    TypeNode checkMethodCallOnMold(const std::string& moldName, const std::string& methodName, CallExpr& call);
    TypeNode checkMethodCallOnFacet(const std::string& facetName, const std::string& methodName, CallExpr& call);
    // Встроенные функции (len, str, range, ...) и методы массивов/строк (xs.append, s.upper, ...).
    bool checkArgCount(CallExpr& e, const std::string& what, size_t lo, size_t hi);
    TypeNode checkBuiltinFunction(CallExpr& e, const BuiltinDef& def);
    TypeNode checkArrayMethod(CallExpr& e, FieldAccessExpr& f, const TypeNode& arrayType);
    TypeNode checkStringMethod(CallExpr& e, FieldAccessExpr& f);

    static TypeNode intType();
    static TypeNode doubleType();
    static TypeNode boolType();
    static TypeNode stringType();
    static TypeNode voidType();
    static TypeNode errorType();
    static TypeNode nullType();
    static TypeNode arrayOf(TypeNode elem);
    static TypeNode named(const std::string& name);
    static bool isNumeric(const TypeNode& t);
};

} // namespace glass

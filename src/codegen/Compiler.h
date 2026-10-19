#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ast/Decl.h"
#include "../ast/Expr.h"
#include "../ast/Stmt.h"
#include "../bytecode/Chunk.h"
#include "../sema/Sema.h"

namespace glass {

// Компилирует уже проверенную (Sema + BodyChecker прошли чисто) программу
// в байткод стековой VM. Поддерживается весь язык этой фазы: molds
// (конструкция объектов, поля, виртуальная диспетчеризация по vtable,
// this/super), массивы (литералы, индексация на чтение/запись, for..in),
// try/catch/throw/finally (тип catch сопоставляется по цепочке
// mold-наследования и facet'ам — см. ClassInfo::ancestry) и интроспекция
// (~layout/~vtable/~address/~heap/~gc — см. prelude/Prelude.h:
// LayoutInfo/VTableInfo/HeapSnapshot, которые VM заполняет напрямую).
// Дальше — уже C API и WPF-часть, не эта фаза (см. README).
//
// При встрече неподдерживаемой конструкции бросает std::runtime_error —
// это внутренняя, а не пользовательская ошибка: Sema/BodyChecker её не
// ловят, потому что на уровне языка конструкция валидна, просто VM для
// неё ещё не готова.
//
// Кодогенерации нужен статический тип каждого выражения (для смещений
// полей и выбора virtual/статического вызова) — он уже посчитан
// BodyChecker'ом и лежит в Expr::resolvedType, пересчитывать заново не
// нужно.
class Compiler : public ExprVisitor, public StmtVisitor {
public:
    explicit Compiler(const Sema& sema);

    CompiledProgram compileProgram(Program& program);

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
    CompiledProgram result_;
    CompiledFunction* currentFn_ = nullptr;
    std::string currentMoldName_;  // пусто вне метода/конструктора/field-init mold'а

    struct LocalScope {
        std::unordered_map<std::string, int> slots;
    };
    std::vector<LocalScope> scopes_;
    int nextSlot_ = 0;

    struct LoopContext {
        std::vector<int> continueJumpsToPatch;
        std::vector<int> breakJumpsToPatch;
    };
    std::vector<LoopContext> loopStack_;

    // >0, пока компилируется try-блок или catch-тело, защищённые finally
    // (см. visit(TryStmt&)). return/break/continue оттуда должны были бы
    // сначала прогнать finally — это пока не реализовано (finally
    // дублируется компилятором только в двух местах: нормальное
    // завершение/после catch и проброс дальше, а не на каждый ранний
    // выход), поэтому visit(ReturnStmt&)/visit(BreakStmt&)/
    // visit(ContinueStmt&) при activeFinallyDepth_ > 0 честно отказывают,
    // а не молча пропускают finally.
    int activeFinallyDepth_ = 0;

    // Индексы CompiledFunction, зарегистрированные для members конкретного
    // mold'а (проход 1, до компиляции тел и до построения ClassInfo).
    struct MoldFunctionIndices {
        int fieldInitIndex = -1;
        int constructorIndex = -1;
        std::unordered_map<std::string, int> methodIndices;
    };
    std::unordered_map<std::string, MoldFunctionIndices> moldFunctionIndices_;

    void pushScope();
    void popScope();
    int declareLocal(const std::string& name);
    std::optional<int> resolveLocal(const std::string& name) const;

    void emit(OpCode op, int operand, int line, int operand2 = 0);
    int emitJump(OpCode op, int line);
    void patchJumpToHere(int instrIndex);
    int addConstant(Value v);

    void compileExpr(Expr& e);
    void compileStmt(Stmt& s);

    int allocateFunctionSlot();
    void registerFunctionSlots(Program& program);
    const ClassInfo& buildClassInfo(const std::string& moldName);
    FieldKind fieldKindOf(const TypeNode& type) const;
    bool methodSatisfiesAnyFacet(const std::string& moldName, const std::string& methodName,
                                  const std::vector<Param>& params, const std::optional<TypeNode>& returnType) const;
    bool facetMatchesSignature(const std::string& facetName, const std::string& methodName,
                                const std::vector<std::string>& paramTypeStrs, const std::string& returnTypeStr) const;
    int findStaticMethodFunctionIndex(const std::string& startMold, const std::string& methodName) const;

    void compileFieldInitFunction(const std::string& moldName);
    void compileMethodBody(const std::string& moldName, MethodDecl& method);
    void compileConstructorBody(const std::string& moldName, ConstructorDecl& ctor);
    void compileTopLevelFunction(FunctionDecl& fn);

    void emitConstruction(const std::string& moldName, CallExpr& call);
    void emitMethodCall(FieldAccessExpr& fieldExpr, CallExpr& call);

    // Значение только что вычислено и сейчас станет текстом (аргумент print, операнд '+' со
    // строкой, str(x), 's += x'). Если статический тип выражения — ссылочный (mold/facet), ставит
    // OpCode::ObjToString, чтобы сработал пользовательский toString() (см. bytecode/Chunk.h).
    void emitObjToStringIfObject(const TypeNode& type, int line);
};

} // namespace glass

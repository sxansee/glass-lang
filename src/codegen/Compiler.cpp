#include "../bytecode/Builtins.h"
#include "Compiler.h"

#include <stdexcept>

#include "../ast/Type.h"

namespace glass {

Compiler::Compiler(const Sema& sema) : sema_(sema) {}

// ---------------------------------------------------------------------
// Инфраструктура: области видимости, эмиссия инструкций
// ---------------------------------------------------------------------

void Compiler::pushScope() { scopes_.push_back(LocalScope{}); }
void Compiler::popScope() { scopes_.pop_back(); }

int Compiler::declareLocal(const std::string& name) {
    int slot = nextSlot_++;
    scopes_.back().slots[name] = slot;
    // Для отладчика (см. capi/) — slot -> имя, как в исходнике. Слоты не
    // переиспользуются, так что это ровно один раз на слот, не диапазон.
    if (currentFn_) {
        if (currentFn_->localNames.size() <= static_cast<size_t>(slot)) {
            currentFn_->localNames.resize(static_cast<size_t>(slot) + 1);
        }
        currentFn_->localNames[static_cast<size_t>(slot)] = name;
    }
    return slot;
}

std::optional<int> Compiler::resolveLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->slots.find(name);
        if (found != it->slots.end()) return found->second;
    }
    return std::nullopt;
}

void Compiler::emit(OpCode op, int operand, int line, int operand2) {
    currentFn_->code.push_back(Instruction{op, operand, operand2, line});
}

int Compiler::emitJump(OpCode op, int line) {
    emit(op, -1, line);
    return static_cast<int>(currentFn_->code.size()) - 1;
}

void Compiler::patchJumpToHere(int instrIndex) {
    currentFn_->code[instrIndex].operand = static_cast<int>(currentFn_->code.size());
}

int Compiler::addConstant(Value v) {
    currentFn_->constants.push_back(std::move(v));
    return static_cast<int>(currentFn_->constants.size()) - 1;
}

void Compiler::compileExpr(Expr& e) {
    e.accept(*this);
    if (e.convertToDouble) {
        // BodyChecker принял int там, где ждут double: превращаем значение сразу, иначе в double-переменной
        // лежало бы целое и d / 2 считалось бы как целочисленное деление.
        emit(OpCode::CallBuiltin, static_cast<int>(Builtin::ToDouble), e.line, 1);
    }
}
void Compiler::compileStmt(Stmt& s) { s.accept(*this); }

// ---------------------------------------------------------------------
// Layout классов
// ---------------------------------------------------------------------

int Compiler::allocateFunctionSlot() {
    result_.functions.push_back(CompiledFunction{});
    return static_cast<int>(result_.functions.size()) - 1;
}

void Compiler::registerFunctionSlots(Program& program) {
    for (auto& decl : program.declarations) {
        if (auto* fn = std::get_if<FunctionDecl>(&decl)) {
            result_.functionIndexByName[fn->name] = allocateFunctionSlot();
        } else if (auto* mold = std::get_if<MoldDecl>(&decl)) {
            MoldFunctionIndices indices;
            indices.fieldInitIndex = allocateFunctionSlot();
            for (auto& member : mold->members) {
                if (auto* method = std::get_if<MethodDecl>(&member)) {
                    indices.methodIndices[method->name] = allocateFunctionSlot();
                } else if (std::get_if<ConstructorDecl>(&member)) {
                    indices.constructorIndex = allocateFunctionSlot();
                }
            }
            moldFunctionIndices_[mold->name] = std::move(indices);
        }
    }
}

FieldKind Compiler::fieldKindOf(const TypeNode& type) const {
    if (type.kind == TypeNode::Kind::Primitive) {
        if (type.name == "int") return FieldKind::Int;
        if (type.name == "double") return FieldKind::Double;
        if (type.name == "bool") return FieldKind::Bool;
    }
    return FieldKind::Reference;  // string, mold, facet, массив, дженерик-параметр
}

bool Compiler::facetMatchesSignature(const std::string& facetName, const std::string& methodName,
                                      const std::vector<std::string>& paramTypeStrs,
                                      const std::string& returnTypeStr) const {
    const FacetDecl* decl = sema_.getFacet(facetName);
    if (!decl) return false;
    for (auto& sig : decl->methods) {
        if (sig.name != methodName) continue;
        if (sig.params.size() != paramTypeStrs.size()) continue;
        bool match = true;
        for (size_t i = 0; i < sig.params.size(); i++) {
            if (typeToString(sig.params[i].type) != paramTypeStrs[i]) {
                match = false;
                break;
            }
        }
        std::string sigReturn = sig.returnType ? typeToString(*sig.returnType) : "";
        if (match && sigReturn == returnTypeStr) return true;
    }
    for (auto& base : decl->baseTypes) {
        if (facetMatchesSignature(base, methodName, paramTypeStrs, returnTypeStr)) return true;
    }
    return false;
}

bool Compiler::methodSatisfiesAnyFacet(const std::string& moldName, const std::string& methodName,
                                       const std::vector<Param>& params,
                                       const std::optional<TypeNode>& returnType) const {
    std::vector<std::string> paramTypeStrs;
    for (auto& p : params) paramTypeStrs.push_back(typeToString(p.type));
    std::string returnTypeStr = returnType ? typeToString(*returnType) : "";

    for (auto& facetName : sema_.getMoldFacets(moldName)) {
        if (facetMatchesSignature(facetName, methodName, paramTypeStrs, returnTypeStr)) return true;
    }
    return false;
}

const ClassInfo& Compiler::buildClassInfo(const std::string& moldName) {
    auto already = result_.classIndexByName.find(moldName);
    if (already != result_.classIndexByName.end()) return result_.classes[static_cast<size_t>(already->second)];

    const MoldDecl* moldDecl = sema_.getMold(moldName);
    ClassInfo info;
    info.name = moldName;

    auto chain = sema_.getMoldChain(moldName);  // [moldName, base, grandbase, ...]
    if (chain.size() > 1) {
        const ClassInfo& baseInfo = buildClassInfo(chain[1]);  // строим базу первой (рекурсивно)
        info.fields = baseInfo.fields;                          // те же смещения
        info.vtable = baseInfo.vtable;                           // отправная точка; override перезапишет ниже
    }

    // ancestry — имена себя + всей цепочки предков + всех facet'ов,
    // реализованных где-либо в этой цепочке (facet сам может не быть
    // mold'ом — сразу оба случая). Нужно исключительно для runtime-проверки
    // "чем является объект" при catch по типу (см. ClassInfo::ancestry).
    info.ancestry = chain;
    for (auto& moldInChain : chain) {
        for (auto& facetName : sema_.getMoldFacets(moldInChain)) {
            info.ancestry.push_back(facetName);
        }
    }

    auto& funcIndices = moldFunctionIndices_.at(moldName);
    info.fieldInitFunctionIndex = funcIndices.fieldInitIndex;
    info.constructorFunctionIndex = funcIndices.constructorIndex;

    for (auto& member : moldDecl->members) {
        if (auto* field = std::get_if<FieldDecl>(&member)) {
            FieldSlot slot;
            slot.name = field->name;
            slot.offset = static_cast<int>(info.fields.size());
            slot.kind = fieldKindOf(field->type);
            info.fields.push_back(slot);
        } else if (auto* method = std::get_if<MethodDecl>(&member)) {
            // toString() без параметров — неявно виртуальный (как метод, реализующий facet): print,
            // конкатенация и str(x) вызывают его через vtable по реальному классу объекта.
            const bool isToString = method->name == "toString" && method->params.empty() &&
                                    method->modifier != MethodModifier::Static;
            bool isVirtual = (method->modifier == MethodModifier::Virtual ||
                               method->modifier == MethodModifier::Override) || isToString ||
                              methodSatisfiesAnyFacet(moldName, method->name, method->params, method->returnType);
            if (isVirtual) {
                info.vtable[method->name] = funcIndices.methodIndices.at(method->name);
            }
            // не-виртуальные методы не входят в vtable — вызываются по
            // напрямую резолвленному индексу функции на месте вызова.
        }
    }

    int idx = static_cast<int>(result_.classes.size());
    result_.classIndexByName[moldName] = idx;
    result_.classes.push_back(std::move(info));
    return result_.classes[static_cast<size_t>(idx)];
}

int Compiler::findStaticMethodFunctionIndex(const std::string& startMold, const std::string& methodName) const {
    for (auto& moldName : sema_.getMoldChain(startMold)) {
        auto it = moldFunctionIndices_.find(moldName);
        if (it == moldFunctionIndices_.end()) continue;
        auto methodIt = it->second.methodIndices.find(methodName);
        if (methodIt != it->second.methodIndices.end()) return methodIt->second;
    }
    throw std::runtime_error("codegen: внутренняя ошибка — метод '" + methodName + "' не найден в цепочке '" +
                              startMold + "'");
}

// ---------------------------------------------------------------------
// Верхний уровень
// ---------------------------------------------------------------------

CompiledProgram Compiler::compileProgram(Program& program) {
    result_ = CompiledProgram{};

    registerFunctionSlots(program);

    for (auto& decl : program.declarations) {
        if (auto* mold = std::get_if<MoldDecl>(&decl)) {
            buildClassInfo(mold->name);
        }
    }

    for (auto& decl : program.declarations) {
        if (auto* fn = std::get_if<FunctionDecl>(&decl)) {
            compileTopLevelFunction(*fn);
        } else if (auto* mold = std::get_if<MoldDecl>(&decl)) {
            compileFieldInitFunction(mold->name);
            for (auto& member : mold->members) {
                if (auto* method = std::get_if<MethodDecl>(&member)) {
                    compileMethodBody(mold->name, *method);
                } else if (auto* ctor = std::get_if<ConstructorDecl>(&member)) {
                    compileConstructorBody(mold->name, *ctor);
                }
            }
        }
    }

    return result_;
}

void Compiler::compileTopLevelFunction(FunctionDecl& fn) {
    CompiledFunction& compiled = result_.functions[static_cast<size_t>(result_.functionIndexByName.at(fn.name))];
    compiled.name = fn.name;
    compiled.paramCount = static_cast<int>(fn.params.size());

    currentFn_ = &compiled;
    currentMoldName_.clear();
    scopes_.clear();
    nextSlot_ = 0;
    loopStack_.clear();
    pushScope();
    for (auto& p : fn.params) declareLocal(p.name);

    compileStmt(*fn.body);
    emit(OpCode::PushConst, addConstant(Value(std::monostate{})), fn.line);
    emit(OpCode::Return, 0, fn.line);

    popScope();
    compiled.localCount = nextSlot_;
    currentFn_ = nullptr;
}

void Compiler::compileFieldInitFunction(const std::string& moldName) {
    const MoldDecl* moldDecl = sema_.getMold(moldName);
    const ClassInfo& info = buildClassInfo(moldName);
    CompiledFunction& fn = result_.functions[static_cast<size_t>(info.fieldInitFunctionIndex)];
    fn.name = moldName + "$fieldinit";
    fn.paramCount = 1;  // this

    currentFn_ = &fn;
    currentMoldName_.clear();  // инициализаторы полей не видят this/другие поля — см. BodyChecker
    scopes_.clear();
    nextSlot_ = 0;
    loopStack_.clear();
    pushScope();
    declareLocal("this");  // слот 0

    auto chain = sema_.getMoldChain(moldName);
    if (chain.size() > 1) {
        const ClassInfo& baseInfo = buildClassInfo(chain[1]);
        emit(OpCode::LoadLocal, 0, moldDecl->line);
        emit(OpCode::Call, baseInfo.fieldInitFunctionIndex, moldDecl->line);
        emit(OpCode::Pop, 0, moldDecl->line);
    }

    for (auto& member : moldDecl->members) {
        if (auto* field = std::get_if<FieldDecl>(&member)) {
            if (field->initExpr) {
                const FieldSlot* slot = info.findField(field->name);
                emit(OpCode::LoadLocal, 0, field->line);
                compileExpr(*field->initExpr);
                emit(OpCode::StoreField, slot->offset, field->line);
                emit(OpCode::Pop, 0, field->line);
            }
        }
    }

    emit(OpCode::PushConst, addConstant(Value(std::monostate{})), moldDecl->line);
    emit(OpCode::Return, 0, moldDecl->line);

    popScope();
    fn.localCount = nextSlot_;
    currentFn_ = nullptr;
}

void Compiler::compileMethodBody(const std::string& moldName, MethodDecl& method) {
    int fnIdx = moldFunctionIndices_.at(moldName).methodIndices.at(method.name);
    CompiledFunction& fn = result_.functions[static_cast<size_t>(fnIdx)];
    fn.name = moldName + "." + method.name;
    fn.paramCount = 1 + static_cast<int>(method.params.size());

    currentFn_ = &fn;
    currentMoldName_ = moldName;
    scopes_.clear();
    nextSlot_ = 0;
    loopStack_.clear();
    pushScope();
    declareLocal("this");
    for (auto& p : method.params) declareLocal(p.name);

    compileStmt(*method.body);
    emit(OpCode::PushConst, addConstant(Value(std::monostate{})), method.line);
    emit(OpCode::Return, 0, method.line);

    popScope();
    fn.localCount = nextSlot_;
    currentMoldName_.clear();
    currentFn_ = nullptr;
}

void Compiler::compileConstructorBody(const std::string& moldName, ConstructorDecl& ctor) {
    int fnIdx = moldFunctionIndices_.at(moldName).constructorIndex;
    CompiledFunction& fn = result_.functions[static_cast<size_t>(fnIdx)];
    fn.name = moldName + "$ctor";
    fn.paramCount = 1 + static_cast<int>(ctor.params.size());

    currentFn_ = &fn;
    currentMoldName_ = moldName;
    scopes_.clear();
    nextSlot_ = 0;
    loopStack_.clear();
    pushScope();
    declareLocal("this");
    for (auto& p : ctor.params) declareLocal(p.name);

    compileStmt(*ctor.body);
    emit(OpCode::PushConst, addConstant(Value(std::monostate{})), ctor.line);
    emit(OpCode::Return, 0, ctor.line);

    popScope();
    fn.localCount = nextSlot_;
    currentMoldName_.clear();
    currentFn_ = nullptr;
}

// ---------------------------------------------------------------------
// Конструкция и вызовы методов
// ---------------------------------------------------------------------

void Compiler::emitConstruction(const std::string& moldName, CallExpr& call) {
    const ClassInfo& info = buildClassInfo(moldName);
    int classIdx = result_.classIndexByName.at(moldName);

    emit(OpCode::NewInstance, classIdx, call.line);
    emit(OpCode::Dup, 0, call.line);
    emit(OpCode::Call, info.fieldInitFunctionIndex, call.line);
    emit(OpCode::Pop, 0, call.line);

    if (info.constructorFunctionIndex >= 0) {
        emit(OpCode::Dup, 0, call.line);
        for (auto& a : call.args) compileExpr(*a);
        emit(OpCode::Call, info.constructorFunctionIndex, call.line);
        emit(OpCode::Pop, 0, call.line);
    } else {
        for (auto& a : call.args) {
            compileExpr(*a);
            emit(OpCode::Pop, 0, call.line);
        }
    }
}

void Compiler::emitObjToStringIfObject(const TypeNode& type, int line) {
    if (type.kind != TypeNode::Kind::Named) return;
    if (sema_.isKnownMold(type.name) || sema_.isKnownFacet(type.name)) {
        emit(OpCode::ObjToString, 0, line);
    }
}

namespace {
bool isStringTypeNode(const TypeNode& t) { return t.kind == TypeNode::Kind::Primitive && t.name == "string"; }
}  // namespace

void Compiler::emitMethodCall(FieldAccessExpr& fieldExpr, CallExpr& call) {
    const TypeNode& receiverType = fieldExpr.target->resolvedType;
    const bool receiverIsArray = receiverType.kind == TypeNode::Kind::Array;
    const bool receiverIsString = receiverType.kind == TypeNode::Kind::Primitive && receiverType.name == "string";
    if (receiverIsArray || receiverIsString) {
        // Встроенный метод: [получатель, аргументы...] → CallBuiltin (результат или null, как у обычной функции)
        const BuiltinDef* def = receiverIsArray ? findArrayMethod(fieldExpr.name) : findStringMethod(fieldExpr.name);
        if (!def) {
            throw std::runtime_error("codegen (" + std::to_string(call.line) + "): неизвестный метод '" + fieldExpr.name + "'");
        }
        compileExpr(*fieldExpr.target);
        for (auto& a : call.args) compileExpr(*a);
        emit(OpCode::CallBuiltin, static_cast<int>(def->id), call.line, static_cast<int>(call.args.size()) + 1);
        return;
    }

    compileExpr(*fieldExpr.target);  // стек: [receiver]
    const TypeNode& targetType = fieldExpr.target->resolvedType;

    bool useVirtual;
    int staticFnIdx = -1;
    if (targetType.kind == TypeNode::Kind::Named && sema_.isKnownFacet(targetType.name)) {
        useVirtual = true;
    } else {
        const ClassInfo& info = buildClassInfo(targetType.name);
        if (info.vtable.count(fieldExpr.name)) {
            useVirtual = true;
        } else {
            useVirtual = false;
            staticFnIdx = findStaticMethodFunctionIndex(targetType.name, fieldExpr.name);
        }
    }

    for (auto& a : call.args) compileExpr(*a);

    if (useVirtual) {
        int nameConstIdx = addConstant(Value(std::make_shared<std::string>(fieldExpr.name)));
        emit(OpCode::CallVirtual, nameConstIdx, call.line, static_cast<int>(call.args.size()));
    } else {
        emit(OpCode::Call, staticFnIdx, call.line);
    }
}

// ---------------------------------------------------------------------
// ExprVisitor
// ---------------------------------------------------------------------

void Compiler::visit(IntLiteralExpr& e) {
    emit(OpCode::PushConst, addConstant(Value(static_cast<long long>(e.value))), e.line);
}
void Compiler::visit(DoubleLiteralExpr& e) { emit(OpCode::PushConst, addConstant(Value(e.value)), e.line); }
void Compiler::visit(BoolLiteralExpr& e) { emit(OpCode::PushConst, addConstant(Value(e.value)), e.line); }
void Compiler::visit(StringLiteralExpr& e) {
    emit(OpCode::PushConst, addConstant(Value(std::make_shared<std::string>(e.value))), e.line);
}
void Compiler::visit(NullLiteralExpr& e) {
    emit(OpCode::PushConst, addConstant(Value(std::monostate{})), e.line);
}

void Compiler::visit(ThisExpr& e) {
    if (currentMoldName_.empty()) {
        throw std::runtime_error("codegen (" + std::to_string(e.line) + "): 'this' вне метода/конструктора");
    }
    emit(OpCode::LoadLocal, 0, e.line);  // this всегда слот 0
}

void Compiler::visit(SuperExpr& e) {
    auto chain = sema_.getMoldChain(currentMoldName_);
    const std::string& baseName = chain[1];  // BodyChecker гарантировал chain.size() >= 2

    if (e.methodName.empty()) {
        const ClassInfo& baseInfo = buildClassInfo(baseName);
        if (baseInfo.constructorFunctionIndex >= 0) {
            emit(OpCode::LoadLocal, 0, e.line);
            for (auto& a : e.args) compileExpr(*a);
            emit(OpCode::Call, baseInfo.constructorFunctionIndex, e.line);
        } else {
            for (auto& a : e.args) {
                compileExpr(*a);
                emit(OpCode::Pop, 0, e.line);
            }
            emit(OpCode::PushConst, addConstant(Value(std::monostate{})), e.line);
        }
    } else {
        // super.method(...) — всегда статическая диспетчеризация, не через vtable.
        int fnIdx = findStaticMethodFunctionIndex(baseName, e.methodName);
        emit(OpCode::LoadLocal, 0, e.line);
        for (auto& a : e.args) compileExpr(*a);
        emit(OpCode::Call, fnIdx, e.line);
    }
}

void Compiler::visit(IdentifierExpr& e) {
    auto slot = resolveLocal(e.name);
    if (slot) {
        emit(OpCode::LoadLocal, *slot, e.line);
        return;
    }
    if (!currentMoldName_.empty()) {
        const ClassInfo& info = buildClassInfo(currentMoldName_);
        if (const FieldSlot* field = info.findField(e.name)) {
            emit(OpCode::LoadLocal, 0, e.line);  // this
            emit(OpCode::LoadField, field->offset, e.line);
            return;
        }
    }
    if (e.name == "PI" || e.name == "E") {
        emit(OpCode::PushConst, addConstant(Value(e.name == "PI" ? 3.14159265358979323846 : 2.71828182845904523536)), e.line);
        return;
    }
    throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неизвестный идентификатор '" + e.name + "'");
}

void Compiler::visit(ArrayLiteralExpr& e) {
    // Элементы — в стек в порядке литерала; NewArray сам заберёт нужное
    // число значений с вершины и построит массив в куче (см. OpCode::NewArray).
    for (auto& elem : e.elements) compileExpr(*elem);
    emit(OpCode::NewArray, static_cast<int>(e.elements.size()), e.line);
}

void Compiler::visit(GlobalIntrospectExpr& e) {
    switch (e.kind) {
        case GlobalIntrospectKind::Heap:
            emit(OpCode::Heap, result_.classIndexByName.at("HeapSnapshot"), e.line);
            return;
        case GlobalIntrospectKind::Gc:
            emit(OpCode::Gc, 0, e.line);
            return;
    }
}

void Compiler::visit(UnaryExpr& e) {
    compileExpr(*e.operand);
    emit(e.op == UnaryOp::Not ? OpCode::Not : OpCode::Neg, 0, e.line);
}

void Compiler::visit(CastExpr& e) {
    compileExpr(*e.operand);
    const TypeNode& from = e.operand->resolvedType;
    const TypeNode& to = e.targetType;
    if (to.kind == TypeNode::Kind::Named) {
        // Приведение ссылочных типов значение не меняет, но во время выполнения проверяется: объект
        // обязан быть экземпляром целевого типа (или его потомка), иначе — ошибка. null проходит всегда.
        emit(OpCode::CheckCast, addConstant(Value(std::make_shared<std::string>(to.name))), e.line);
        return;
    }
    if (to.kind != TypeNode::Kind::Primitive) return;  // массивы: без преобразования и без проверки
    if (from.kind == TypeNode::Kind::Primitive && from.name == to.name) return;
    if (to.name == "string") emitObjToStringIfObject(from, e.line);  // (string) obj — через toString()
    Builtin conversion = to.name == "int" ? Builtin::ToInt : to.name == "double" ? Builtin::ToDouble
                       : to.name == "bool" ? Builtin::ToBool : Builtin::Str;
    emit(OpCode::CallBuiltin, static_cast<int>(conversion), e.line, 1);
}

void Compiler::visit(BinaryExpr& e) {
    if (e.op == BinaryOp::And) {
        compileExpr(*e.left);
        int jump = emitJump(OpCode::JumpIfFalseOrPop, e.line);
        compileExpr(*e.right);
        patchJumpToHere(jump);
        return;
    }
    if (e.op == BinaryOp::Or) {
        compileExpr(*e.left);
        int jump = emitJump(OpCode::JumpIfTrueOrPop, e.line);
        compileExpr(*e.right);
        patchJumpToHere(jump);
        return;
    }

    // Склейка со строкой: объект с toString() вставляется своим текстом (как в print).
    const bool textConcat = e.op == BinaryOp::Add &&
                            (isStringTypeNode(e.left->resolvedType) || isStringTypeNode(e.right->resolvedType));
    compileExpr(*e.left);
    if (textConcat) emitObjToStringIfObject(e.left->resolvedType, e.line);
    compileExpr(*e.right);
    if (textConcat) emitObjToStringIfObject(e.right->resolvedType, e.line);
    switch (e.op) {
        case BinaryOp::Add: emit(OpCode::Add, 0, e.line); break;
        case BinaryOp::Sub: emit(OpCode::Sub, 0, e.line); break;
        case BinaryOp::Mul: emit(OpCode::Mul, 0, e.line); break;
        case BinaryOp::Div: emit(OpCode::Div, 0, e.line); break;
        case BinaryOp::Mod: emit(OpCode::Mod, 0, e.line); break;
        case BinaryOp::Eq: emit(OpCode::Eq, 0, e.line); break;
        case BinaryOp::NotEq: emit(OpCode::NotEq, 0, e.line); break;
        case BinaryOp::Less: emit(OpCode::Lt, 0, e.line); break;
        case BinaryOp::Greater: emit(OpCode::Gt, 0, e.line); break;
        case BinaryOp::LessEq: emit(OpCode::LtEq, 0, e.line); break;
        case BinaryOp::GreaterEq: emit(OpCode::GtEq, 0, e.line); break;
        case BinaryOp::And:
        case BinaryOp::Or:
            break;
    }
}

void Compiler::visit(AssignExpr& e) {
    // 's += obj': правая часть склеивается со строкой — объект с toString() вставляется своим текстом.
    const bool textAppend = e.op == AssignOp::AddAssign && isStringTypeNode(e.target->resolvedType);
    auto compileCompoundRhs = [&]() {
        compileExpr(*e.value);
        if (textAppend) emitObjToStringIfObject(e.value->resolvedType, e.line);
    };
    if (auto* fieldTarget = dynamic_cast<FieldAccessExpr*>(e.target.get())) {
        compileExpr(*fieldTarget->target);  // стек: [obj]
        const ClassInfo& info = buildClassInfo(fieldTarget->target->resolvedType.name);
        const FieldSlot* slot = info.findField(fieldTarget->name);
        if (!slot) {
            throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неизвестное поле '" +
                                      fieldTarget->name + "'");
        }
        if (e.op == AssignOp::Assign) {
            compileExpr(*e.value);  // стек: [obj, value]
        } else {
            emit(OpCode::Dup, 0, e.line);                  // [obj, obj]
            emit(OpCode::LoadField, slot->offset, e.line);  // [obj, current]
            compileCompoundRhs();                           // [obj, current, rhs]
            switch (e.op) {
                case AssignOp::AddAssign: emit(OpCode::Add, 0, e.line); break;
                case AssignOp::SubAssign: emit(OpCode::Sub, 0, e.line); break;
                case AssignOp::MulAssign: emit(OpCode::Mul, 0, e.line); break;
                case AssignOp::DivAssign: emit(OpCode::Div, 0, e.line); break;
                case AssignOp::Assign: break;
            }
        }
        emit(OpCode::StoreField, slot->offset, e.line);  // потребляет [obj, value], кладёт value обратно
        return;
    }

    if (auto* idxTarget = dynamic_cast<IndexExpr*>(e.target.get())) {
        compileExpr(*idxTarget->target);  // стек: [массив]
        compileExpr(*idxTarget->index);   // стек: [массив, индекс]
        if (e.op == AssignOp::Assign) {
            compileExpr(*e.value);  // стек: [массив, индекс, значение]
        } else {
            // Dup2, а не Dup: нужно И массив, И индекс ещё раз, чтобы
            // прочитать текущий элемент перед вычислением нового значения.
            emit(OpCode::Dup2, 0, e.line);        // [массив, индекс, массив, индекс]
            emit(OpCode::LoadIndex, 0, e.line);   // [массив, индекс, текущее]
            compileCompoundRhs();                  // [массив, индекс, текущее, rhs]
            switch (e.op) {
                case AssignOp::AddAssign: emit(OpCode::Add, 0, e.line); break;
                case AssignOp::SubAssign: emit(OpCode::Sub, 0, e.line); break;
                case AssignOp::MulAssign: emit(OpCode::Mul, 0, e.line); break;
                case AssignOp::DivAssign: emit(OpCode::Div, 0, e.line); break;
                case AssignOp::Assign: break;
            }
        }
        emit(OpCode::StoreIndex, 0, e.line);  // потребляет [массив, индекс, значение], кладёт значение обратно
        return;
    }

    auto* idTarget = dynamic_cast<IdentifierExpr*>(e.target.get());
    if (!idTarget) {
        throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неподдерживаемая цель присваивания");
    }
    auto slot = resolveLocal(idTarget->name);
    if (!slot) {
        // Не локальная переменная — значит поле текущего mold'а через неявный this.
        if (currentMoldName_.empty()) {
            throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неизвестная переменная '" +
                                      idTarget->name + "'");
        }
        const ClassInfo& info = buildClassInfo(currentMoldName_);
        const FieldSlot* field = info.findField(idTarget->name);
        if (!field) {
            throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неизвестная переменная '" +
                                      idTarget->name + "'");
        }
        emit(OpCode::LoadLocal, 0, e.line);  // this
        if (e.op == AssignOp::Assign) {
            compileExpr(*e.value);
        } else {
            emit(OpCode::Dup, 0, e.line);
            emit(OpCode::LoadField, field->offset, e.line);
            compileCompoundRhs();
            switch (e.op) {
                case AssignOp::AddAssign: emit(OpCode::Add, 0, e.line); break;
                case AssignOp::SubAssign: emit(OpCode::Sub, 0, e.line); break;
                case AssignOp::MulAssign: emit(OpCode::Mul, 0, e.line); break;
                case AssignOp::DivAssign: emit(OpCode::Div, 0, e.line); break;
                case AssignOp::Assign: break;
            }
        }
        emit(OpCode::StoreField, field->offset, e.line);
        return;
    }

    if (e.op == AssignOp::Assign) {
        compileExpr(*e.value);
    } else {
        emit(OpCode::LoadLocal, *slot, e.line);
        compileCompoundRhs();
        switch (e.op) {
            case AssignOp::AddAssign: emit(OpCode::Add, 0, e.line); break;
            case AssignOp::SubAssign: emit(OpCode::Sub, 0, e.line); break;
            case AssignOp::MulAssign: emit(OpCode::Mul, 0, e.line); break;
            case AssignOp::DivAssign: emit(OpCode::Div, 0, e.line); break;
            case AssignOp::Assign: break;
        }
    }
    emit(OpCode::StoreLocal, *slot, e.line);
}

void Compiler::visit(CallExpr& e) {
    if (auto* idExpr = dynamic_cast<IdentifierExpr*>(e.callee.get())) {
        const std::string& name = idExpr->name;

        if (name == "print") {
            // print(a, b, c): все аргументы в стек, Print снимает их и печатает через пробел
            for (auto& a : e.args) {
                compileExpr(*a);
                emitObjToStringIfObject(a->resolvedType, e.line);  // объект с toString() печатается им
            }
            emit(OpCode::Print, static_cast<int>(e.args.size()), e.line);
            return;
        }

        auto fnIt = result_.functionIndexByName.find(name);
        if (fnIt != result_.functionIndexByName.end()) {
            for (auto& a : e.args) compileExpr(*a);
            emit(OpCode::Call, fnIt->second, e.line);
            return;
        }

        if (sema_.isKnownMold(name)) {
            emitConstruction(name, e);
            return;
        }

        if (const BuiltinDef* def = findBuiltinFunction(name)) {
            for (auto& a : e.args) {
                compileExpr(*a);
                if (def->id == Builtin::Str) emitObjToStringIfObject(a->resolvedType, e.line);
            }
            emit(OpCode::CallBuiltin, static_cast<int>(def->id), e.line, static_cast<int>(e.args.size()));
            return;
        }

        throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неизвестная функция или mold '" + name +
                                  "'");
    }

    if (auto* fieldExpr = dynamic_cast<FieldAccessExpr*>(e.callee.get())) {
        emitMethodCall(*fieldExpr, e);
        return;
    }

    throw std::runtime_error("codegen (" + std::to_string(e.line) + "): это выражение не может быть вызвано");
}

void Compiler::visit(FieldAccessExpr& e) {
    const TypeNode& targetType = e.target->resolvedType;
    if (targetType.kind == TypeNode::Kind::Array || (targetType.kind == TypeNode::Kind::Primitive && targetType.name == "string")) {
        compileExpr(*e.target);              // xs.length / s.length — единственное «поле» массива и строки
        emit(OpCode::ArrayLen, 0, e.line);
        return;
    }
    compileExpr(*e.target);  // стек: [obj]
    const ClassInfo& info = buildClassInfo(e.target->resolvedType.name);
    const FieldSlot* slot = info.findField(e.name);
    if (!slot) {
        throw std::runtime_error("codegen (" + std::to_string(e.line) + "): неизвестное поле '" + e.name + "'");
    }
    emit(OpCode::LoadField, slot->offset, e.line);
}

void Compiler::visit(IndexExpr& e) {
    compileExpr(*e.target);  // стек: [массив или строка]
    compileExpr(*e.index);   // стек: [массив, индекс]
    emit(OpCode::LoadIndex, 0, e.line);
}

void Compiler::visit(SliceExpr& e) {
    compileExpr(*e.target);  // стек: [массив или строка]
    int present = 0;         // какие из start/stop/step заданы (биты 1, 2, 4) — именно они лежат в стеке
    if (e.start) { compileExpr(*e.start); present |= 1; }
    if (e.stop)  { compileExpr(*e.stop);  present |= 2; }
    if (e.step)  { compileExpr(*e.step);  present |= 4; }
    emit(OpCode::Slice, present, e.line);
}

void Compiler::visit(IntrospectFieldExpr& e) {
    compileExpr(*e.target);  // стек: [объект] — Layout/Vtable/Address сами его снимут
    switch (e.kind) {
        case IntrospectKind::Layout:
            emit(OpCode::Layout, result_.classIndexByName.at("LayoutInfo"), e.line);
            return;
        case IntrospectKind::Vtable:
            emit(OpCode::Vtable, result_.classIndexByName.at("VTableInfo"), e.line);
            return;
        case IntrospectKind::Address:
            emit(OpCode::Address, 0, e.line);
            return;
    }
}

// ---------------------------------------------------------------------
// StmtVisitor
// ---------------------------------------------------------------------

void Compiler::visit(LetStmt& s) {
    int slot = declareLocal(s.name);
    if (s.initExpr) {
        compileExpr(*s.initExpr);
    } else {
        emit(OpCode::PushConst, addConstant(Value(std::monostate{})), s.line);
    }
    emit(OpCode::StoreLocal, slot, s.line);
    emit(OpCode::Pop, 0, s.line);
}

void Compiler::visit(BlockStmt& s) {
    pushScope();
    for (auto& st : s.statements) compileStmt(*st);
    popScope();
}

void Compiler::visit(IfStmt& s) {
    compileExpr(*s.condition);
    int elseJump = emitJump(OpCode::JumpIfFalse, s.line);
    compileStmt(*s.thenBranch);
    int endJump = emitJump(OpCode::Jump, s.line);
    patchJumpToHere(elseJump);
    if (s.elseBranch) compileStmt(*s.elseBranch);
    patchJumpToHere(endJump);
}

void Compiler::visit(WhileStmt& s) {
    int loopStart = static_cast<int>(currentFn_->code.size());
    compileExpr(*s.condition);
    int exitJump = emitJump(OpCode::JumpIfFalse, s.line);

    loopStack_.push_back(LoopContext{});
    compileStmt(*s.body);
    for (int idx : loopStack_.back().continueJumpsToPatch) {
        currentFn_->code[idx].operand = loopStart;
    }
    emit(OpCode::Jump, loopStart, s.line);

    patchJumpToHere(exitJump);
    for (int idx : loopStack_.back().breakJumpsToPatch) patchJumpToHere(idx);
    loopStack_.pop_back();
}

void Compiler::visit(ForStmt& s) {
    pushScope();

    // Счётный цикл: for i in a..b, for i in range(n), for i in range(a, b). Массив для range()
    // не создаётся. Счётчик ($forIdx) скрытый, а переменная пользователя получает его значение
    // заново на каждой итерации — как в Python: i += 2 в теле не ломает обход.
    Expr* startExpr = nullptr;
    Expr* endExpr = nullptr;
    if (s.rangeEnd) {
        startExpr = s.iterable.get();
        endExpr = s.rangeEnd.get();
    } else if (auto* call = dynamic_cast<CallExpr*>(s.iterable.get())) {
        auto* callee = dynamic_cast<IdentifierExpr*>(call->callee.get());
        bool isBuiltinRange = callee && callee->name == "range" && !result_.functionIndexByName.count("range") &&
                              !sema_.isKnownMold("range") && !resolveLocal("range");
        if (isBuiltinRange && call->args.size() == 1) {
            endExpr = call->args[0].get();
        } else if (isBuiltinRange && call->args.size() == 2) {
            startExpr = call->args[0].get();
            endExpr = call->args[1].get();
        }
    }

    if (endExpr) {
        int idxSlot = declareLocal("$forIdx");
        if (startExpr) compileExpr(*startExpr);
        else emit(OpCode::PushConst, addConstant(Value(static_cast<long long>(0))), s.line);
        emit(OpCode::StoreLocal, idxSlot, s.line);
        emit(OpCode::Pop, 0, s.line);

        int endSlot = declareLocal("$forEnd");
        compileExpr(*endExpr);
        emit(OpCode::StoreLocal, endSlot, s.line);
        emit(OpCode::Pop, 0, s.line);

        int varSlot = declareLocal(s.varName);

        int loopStart = static_cast<int>(currentFn_->code.size());
        emit(OpCode::LoadLocal, idxSlot, s.line);
        emit(OpCode::LoadLocal, endSlot, s.line);
        emit(OpCode::Lt, 0, s.line);
        int exitJump = emitJump(OpCode::JumpIfFalse, s.line);

        emit(OpCode::LoadLocal, idxSlot, s.line);
        emit(OpCode::StoreLocal, varSlot, s.line);
        emit(OpCode::Pop, 0, s.line);

        loopStack_.push_back(LoopContext{});
        compileStmt(*s.body);

        int incrementStart = static_cast<int>(currentFn_->code.size());
        for (int idx : loopStack_.back().continueJumpsToPatch) {
            currentFn_->code[idx].operand = incrementStart;
        }
        emit(OpCode::LoadLocal, idxSlot, s.line);
        emit(OpCode::PushConst, addConstant(Value(static_cast<long long>(1))), s.line);
        emit(OpCode::Add, 0, s.line);
        emit(OpCode::StoreLocal, idxSlot, s.line);
        emit(OpCode::Pop, 0, s.line);
        emit(OpCode::Jump, loopStart, s.line);

        patchJumpToHere(exitJump);
        for (int idx : loopStack_.back().breakJumpsToPatch) patchJumpToHere(idx);
        loopStack_.pop_back();
        popScope();
        return;
    }

    // 'for x in массив' и 'for c in строка': BodyChecker уже проверил тип итерируемого.
    // Коллекция вычисляется ОДИН раз ($forArr), длина читается один раз ($forLen) — OpCode::ArrayLen
    // умеет и массивы, и строки (в символах). Переменная пользователя получает элемент заново на каждом шаге.
    int arrSlot = declareLocal("$forArr");
    compileExpr(*s.iterable);
    emit(OpCode::StoreLocal, arrSlot, s.line);
    emit(OpCode::Pop, 0, s.line);

    int idxSlot = declareLocal("$forIdx");
    emit(OpCode::PushConst, addConstant(Value(static_cast<long long>(0))), s.line);
    emit(OpCode::StoreLocal, idxSlot, s.line);
    emit(OpCode::Pop, 0, s.line);

    int lenSlot = declareLocal("$forLen");
    emit(OpCode::LoadLocal, arrSlot, s.line);
    emit(OpCode::ArrayLen, 0, s.line);
    emit(OpCode::StoreLocal, lenSlot, s.line);
    emit(OpCode::Pop, 0, s.line);

    int varSlot = declareLocal(s.varName);

    int loopStart = static_cast<int>(currentFn_->code.size());
    emit(OpCode::LoadLocal, idxSlot, s.line);
    emit(OpCode::LoadLocal, lenSlot, s.line);
    emit(OpCode::Lt, 0, s.line);
    int exitJump = emitJump(OpCode::JumpIfFalse, s.line);

    emit(OpCode::LoadLocal, arrSlot, s.line);
    emit(OpCode::LoadLocal, idxSlot, s.line);
    emit(OpCode::LoadIndex, 0, s.line);
    emit(OpCode::StoreLocal, varSlot, s.line);
    emit(OpCode::Pop, 0, s.line);

    loopStack_.push_back(LoopContext{});
    compileStmt(*s.body);

    int incrementStart = static_cast<int>(currentFn_->code.size());
    for (int idx : loopStack_.back().continueJumpsToPatch) {
        currentFn_->code[idx].operand = incrementStart;
    }
    emit(OpCode::LoadLocal, idxSlot, s.line);
    emit(OpCode::PushConst, addConstant(Value(static_cast<long long>(1))), s.line);
    emit(OpCode::Add, 0, s.line);
    emit(OpCode::StoreLocal, idxSlot, s.line);
    emit(OpCode::Pop, 0, s.line);
    emit(OpCode::Jump, loopStart, s.line);

    patchJumpToHere(exitJump);
    for (int idx : loopStack_.back().breakJumpsToPatch) patchJumpToHere(idx);
    loopStack_.pop_back();
    popScope();
}

void Compiler::visit(ReturnStmt& s) {
    if (activeFinallyDepth_ > 0) {
        throw std::runtime_error("codegen (" + std::to_string(s.line) +
                                  "): 'return' внутри try/catch с finally пока не поддерживается — "
                                  "finally в этом случае гарантированно не выполнился бы");
    }
    if (s.value) {
        compileExpr(*s.value);
    } else {
        emit(OpCode::PushConst, addConstant(Value(std::monostate{})), s.line);
    }
    emit(OpCode::Return, 0, s.line);
}

void Compiler::visit(BreakStmt& s) {
    if (loopStack_.empty()) {
        throw std::runtime_error("codegen (" + std::to_string(s.line) + "): 'break' вне цикла");
    }
    if (activeFinallyDepth_ > 0) {
        throw std::runtime_error("codegen (" + std::to_string(s.line) +
                                  "): 'break' внутри try/catch с finally пока не поддерживается — "
                                  "finally в этом случае гарантированно не выполнился бы");
    }
    int idx = emitJump(OpCode::Jump, s.line);
    loopStack_.back().breakJumpsToPatch.push_back(idx);
}

void Compiler::visit(ContinueStmt& s) {
    if (loopStack_.empty()) {
        throw std::runtime_error("codegen (" + std::to_string(s.line) + "): 'continue' вне цикла");
    }
    if (activeFinallyDepth_ > 0) {
        throw std::runtime_error("codegen (" + std::to_string(s.line) +
                                  "): 'continue' внутри try/catch с finally пока не поддерживается — "
                                  "finally в этом случае гарантированно не выполнился бы");
    }
    int idx = emitJump(OpCode::Jump, s.line);
    loopStack_.back().continueJumpsToPatch.push_back(idx);
}

void Compiler::visit(TryStmt& s) {
    // Компилируем finally (если есть) ДВАЖДЫ — это стандартная техника
    // (то же делают компиляторы Java): один раз на пути "try завершился
    // нормально или исключение поймано одним из catch", второй раз на
    // пути "ни один catch этого try не подошёл, но finally всё равно
    // обязан выполниться перед тем, как исключение пойдёт дальше". Так
    // finally не нужен отдельный опкод/протокол возврата — только обычная
    // линейная генерация кода, дважды.
    bool hasFinally = (s.finallyBlock != nullptr);

    int tryStart = static_cast<int>(currentFn_->code.size());
    if (hasFinally) activeFinallyDepth_++;
    compileStmt(*s.tryBlock);
    if (hasFinally) activeFinallyDepth_--;
    std::vector<int> jumpsToAfterAll;
    jumpsToAfterAll.push_back(emitJump(OpCode::Jump, s.line));
    int tryEnd = static_cast<int>(currentFn_->code.size());

    std::vector<ExceptionHandler> handlersForThisTry;

    for (auto& c : s.catches) {
        int handlerPC = static_cast<int>(currentFn_->code.size());
        pushScope();
        // VM уже положил пойманное значение в этот слот до перехода сюда
        // (см. VM::unwindToHandler) — тело компилируем как обычный блок.
        int slot = declareLocal(c.varName);
        if (hasFinally) activeFinallyDepth_++;
        compileStmt(*c.body);
        if (hasFinally) activeFinallyDepth_--;
        popScope();
        jumpsToAfterAll.push_back(emitJump(OpCode::Jump, s.line));

        ExceptionHandler h;
        h.tryStart = tryStart;
        h.tryEnd = tryEnd;
        h.catchTypeConstant = addConstant(Value(std::make_shared<std::string>(c.type.name)));
        h.handlerPC = handlerPC;
        h.localSlot = slot;
        handlersForThisTry.push_back(h);
    }

    if (hasFinally) {
        // finally-проброс: ни один catch не подошёл. handlerPC здесь лежит
        // ЗА пределами [tryStart, tryEnd) этого try — значит Throw ниже,
        // выполнившись, уже не найдёт (и не должен найти) обработчики
        // ЭТОГО ЖЕ try повторно, а продолжит поиск снаружи, как и нужно.
        int rethrowPC = static_cast<int>(currentFn_->code.size());
        compileStmt(*s.finallyBlock);
        emit(OpCode::Throw, 0, s.line);

        ExceptionHandler passthrough;
        passthrough.tryStart = tryStart;
        passthrough.tryEnd = tryEnd;
        passthrough.catchTypeConstant = -1;
        passthrough.handlerPC = rethrowPC;
        passthrough.localSlot = -1;
        handlersForThisTry.push_back(passthrough);
    }

    for (int idx : jumpsToAfterAll) patchJumpToHere(idx);
    if (hasFinally) compileStmt(*s.finallyBlock);  // тот же исходник, второй раз — путь "не бросало / поймано"

    for (auto& h : handlersForThisTry) currentFn_->handlers.push_back(h);
}

void Compiler::visit(ThrowStmt& s) {
    compileExpr(*s.value);
    emit(OpCode::Throw, 0, s.line);
}

void Compiler::visit(ExprStmt& s) {
    compileExpr(*s.expr);
    emit(OpCode::Pop, 0, s.line);
}

void Compiler::visit(EmptyStmt&) {}

} // namespace glass

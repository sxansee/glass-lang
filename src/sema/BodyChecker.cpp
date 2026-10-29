#include "BodyChecker.h"

#include "../ast/Type.h"
#include "../bytecode/Builtins.h"

namespace glass {

namespace {

bool isErrT(const TypeNode& t) { return t.name == "<error>"; }
bool isVoidT(const TypeNode& t) { return t.kind == TypeNode::Kind::Primitive && t.name == "void"; }
bool isStringT(const TypeNode& t) { return t.kind == TypeNode::Kind::Primitive && t.name == "string"; }
bool isIntT(const TypeNode& t) { return t.kind == TypeNode::Kind::Primitive && t.name == "int"; }
bool isDoubleT(const TypeNode& t) { return t.kind == TypeNode::Kind::Primitive && t.name == "double"; }
bool isArrayT(const TypeNode& t) { return t.kind == TypeNode::Kind::Array && t.elementType != nullptr; }
// int, double, bool, string — то, что умеют друг в друга преобразовывать int()/double()/bool()/str().
bool isConvertible(const TypeNode& t) {
    return t.kind == TypeNode::Kind::Primitive && (t.name == "int" || t.name == "double" || t.name == "bool" || t.name == "string");
}

// 1 аргумент, 2 аргумента, 5 аргументов
std::string argWord(size_t n) {
    size_t last2 = n % 100, last = n % 10;
    const char* word = (last2 >= 11 && last2 <= 14) ? "аргументов"
                     : last == 1                      ? "аргумент"
                     : (last >= 2 && last <= 4)       ? "аргумента"
                                                      : "аргументов";
    return std::to_string(n) + " " + word;
}

// ---------------------------------------------------------------------
// «Все пути возвращают значение»
//
// Тот же анализ, что «can complete normally» в Java и C#, только с другого конца: alwaysExits(s) —
// после оператора s выполнение НИКОГДА не идёт дальше. Оно либо вернулось (return), либо бросило
// исключение (throw), либо осталось в бесконечном цикле. Условия не вычисляются; единственное
// исключение — литерал true в while.
// ---------------------------------------------------------------------

// Есть ли в операторе break, который выходит из ЭТОГО цикла. Ищем через блоки, if и try; во вложенные
// while/for не заходим: их break выходит из них самих.
bool containsBreak(const Stmt& s) {
    if (dynamic_cast<const BreakStmt*>(&s)) return true;
    if (auto* block = dynamic_cast<const BlockStmt*>(&s)) {
        for (auto& st : block->statements) {
            if (containsBreak(*st)) return true;
        }
        return false;
    }
    if (auto* ifs = dynamic_cast<const IfStmt*>(&s)) {
        return (ifs->thenBranch && containsBreak(*ifs->thenBranch)) || (ifs->elseBranch && containsBreak(*ifs->elseBranch));
    }
    if (auto* tr = dynamic_cast<const TryStmt*>(&s)) {
        if (tr->tryBlock && containsBreak(*tr->tryBlock)) return true;
        for (auto& c : tr->catches) {
            if (c.body && containsBreak(*c.body)) return true;
        }
        return tr->finallyBlock && containsBreak(*tr->finallyBlock);
    }
    return false;  // while/for — свой break; у остальных операторов вложенных операторов нет
}

bool alwaysExits(const Stmt& s) {
    if (dynamic_cast<const ReturnStmt*>(&s) || dynamic_cast<const ThrowStmt*>(&s)) return true;
    if (auto* block = dynamic_cast<const BlockStmt*>(&s)) {
        // Достаточно одного оператора, после которого дальше хода нет: всё, что ниже, недостижимо.
        for (auto& st : block->statements) {
            if (alwaysExits(*st)) return true;
        }
        return false;
    }
    if (auto* ifs = dynamic_cast<const IfStmt*>(&s)) {
        // Без else условие может не выполниться; else if — это вложенный IfStmt в elseBranch.
        return ifs->thenBranch && ifs->elseBranch && alwaysExits(*ifs->thenBranch) && alwaysExits(*ifs->elseBranch);
    }
    if (auto* wh = dynamic_cast<const WhileStmt*>(&s)) {
        auto* lit = dynamic_cast<const BoolLiteralExpr*>(wh->condition.get());
        return lit && lit->value && wh->body && !containsBreak(*wh->body);
    }
    if (auto* tr = dynamic_cast<const TryStmt*>(&s)) {
        if (tr->finallyBlock && alwaysExits(*tr->finallyBlock)) return true;
        if (!tr->tryBlock || !alwaysExits(*tr->tryBlock)) return false;
        for (auto& c : tr->catches) {
            if (!c.body || !alwaysExits(*c.body)) return false;
        }
        return true;
    }
    // for — может не выполниться ни разу; let, выражения, break, continue и пустой оператор идут дальше.
    return false;
}

}  // namespace

BodyChecker::BodyChecker(const Sema& sema) : sema_(sema) {}

// ---------------------------------------------------------------------
// Типы-константы
// ---------------------------------------------------------------------

TypeNode BodyChecker::intType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "int";
    return t;
}
TypeNode BodyChecker::doubleType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "double";
    return t;
}
TypeNode BodyChecker::boolType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "bool";
    return t;
}
TypeNode BodyChecker::stringType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "string";
    return t;
}
TypeNode BodyChecker::voidType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "void";
    return t;
}
TypeNode BodyChecker::errorType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "<error>";
    return t;
}
TypeNode BodyChecker::nullType() {
    TypeNode t;
    t.kind = TypeNode::Kind::Primitive;
    t.name = "<null>";
    return t;
}
TypeNode BodyChecker::arrayOf(TypeNode elem) {
    TypeNode t;
    t.kind = TypeNode::Kind::Array;
    t.elementType = std::make_shared<TypeNode>(std::move(elem));
    return t;
}
TypeNode BodyChecker::named(const std::string& name) {
    TypeNode t;
    t.kind = TypeNode::Kind::Named;
    t.name = name;
    return t;
}
bool BodyChecker::isNumeric(const TypeNode& t) {
    return t.kind == TypeNode::Kind::Primitive && (t.name == "int" || t.name == "double");
}

// ---------------------------------------------------------------------
// Области видимости
// ---------------------------------------------------------------------

void BodyChecker::pushScope() {
    Scope* parent = scopeStack_.empty() ? nullptr : scopeStack_.back().get();
    auto s = std::make_unique<Scope>();
    s->parent = parent;
    scopeStack_.push_back(std::move(s));
}

void BodyChecker::popScope() {
    if (!scopeStack_.empty()) scopeStack_.pop_back();
}

void BodyChecker::defineVar(const std::string& name, const TypeNode& type, int line, int col) {
    if (scopeStack_.empty()) pushScope();
    auto& vars = scopeStack_.back()->vars;
    if (!vars.emplace(name, type).second) {
        error(line, col, "переменная '" + name + "' уже объявлена в этой области видимости");
    }
}

std::optional<TypeNode> BodyChecker::lookupVar(const std::string& name) const {
    for (Scope* s = scopeStack_.empty() ? nullptr : scopeStack_.back().get(); s != nullptr; s = s->parent) {
        auto it = s->vars.find(name);
        if (it != s->vars.end()) return it->second;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------
// Вспомогательное
// ---------------------------------------------------------------------

void BodyChecker::error(int line, int col, const std::string& message) {
    errors_.push_back(std::to_string(line) + ":" + std::to_string(col) + ": " + message);
}

TypeNode BodyChecker::checkExpr(Expr& e) {
    e.accept(*this);
    e.resolvedType = lastType_;
    return lastType_;
}

TypeNode BodyChecker::checkExprAgainstExpected(Expr& e, const TypeNode& expected) {
    if (auto* arr = dynamic_cast<ArrayLiteralExpr*>(&e)) {
        if (expected.kind == TypeNode::Kind::Array && expected.elementType) {
            for (auto& el : arr->elements) {
                checkExprAgainstExpected(*el, *expected.elementType);
            }
            lastType_ = expected;
            e.resolvedType = expected;
            return expected;
        }
    }
    TypeNode actual = checkExpr(e);
    if (!isAssignable(actual, expected)) {
        error(e.line, e.column,
              "тип '" + typeToString(actual) + "' не совместим с ожидаемым '" + typeToString(expected) + "'");
    } else if (isIntT(actual) && isDoubleT(expected)) {
        e.convertToDouble = true;  // неявное расширение: кодогенерация превратит значение в double
    }
    return actual;
}

void BodyChecker::checkStmt(Stmt& s) { s.accept(*this); }

bool BodyChecker::isGenericParam(const std::string& name) const {
    for (auto& g : currentGenericScope_)
        if (g == name) return true;
    return false;
}

bool BodyChecker::isKnownType(const TypeNode& t) const {
    if (t.kind == TypeNode::Kind::Primitive) return true;
    if (t.kind == TypeNode::Kind::Array) return t.elementType && isKnownType(*t.elementType);
    return sema_.isKnownMold(t.name) || sema_.isKnownFacet(t.name) || isGenericParam(t.name);
}

bool BodyChecker::facetExtendsOrIs(const std::string& facetName, const std::string& target) const {
    if (facetName == target) return true;
    const FacetDecl* decl = sema_.getFacet(facetName);
    if (!decl) return false;
    for (auto& base : decl->baseTypes) {
        if (facetExtendsOrIs(base, target)) return true;
    }
    return false;
}

bool BodyChecker::moldImplementsFacet(const std::string& moldName, const std::string& facetName) const {
    for (auto& ancestorMold : sema_.getMoldChain(moldName)) {
        for (auto& directFacet : sema_.getMoldFacets(ancestorMold)) {
            if (facetExtendsOrIs(directFacet, facetName)) return true;
        }
    }
    return false;
}

bool BodyChecker::isAssignable(const TypeNode& from, const TypeNode& to) const {
    if (from.name == "<error>" || to.name == "<error>") return true;
    if (from.name == "<null>") {
        return to.kind == TypeNode::Kind::Named || to.kind == TypeNode::Kind::Array;
    }
    if (from.kind == TypeNode::Kind::Primitive && to.kind == TypeNode::Kind::Primitive) {
        if (from.name == to.name) return true;
        if (from.name == "int" && to.name == "double") return true;  // числовое расширение
        return false;
    }
    if (from.kind == TypeNode::Kind::Array && to.kind == TypeNode::Kind::Array) {
        if (!from.elementType || !to.elementType) return false;
        return typeToString(*from.elementType) == typeToString(*to.elementType);
    }
    if (from.kind == TypeNode::Kind::Named && to.kind == TypeNode::Kind::Named) {
        if (from.name == to.name) return true;
        if (isGenericParam(from.name) || isGenericParam(to.name)) return true;
        if (sema_.isKnownFacet(to.name) && sema_.isKnownMold(from.name)) {
            return moldImplementsFacet(from.name, to.name);
        }
        if (sema_.isKnownMold(to.name) && sema_.isKnownMold(from.name)) {
            for (auto& ancestor : sema_.getMoldChain(from.name)) {
                if (ancestor == to.name) return true;
            }
            return false;
        }
        if (sema_.isKnownFacet(to.name) && sema_.isKnownFacet(from.name)) {
            return facetExtendsOrIs(from.name, to.name);
        }
        // Неизвестный (не смоделированный) именованный тип с любой из сторон
        // — считаем совместимым, чтобы не плодить ложные ошибки поверх уже
        // известного пробела (см. комментарий в BodyChecker.h).
        if (!sema_.isKnownMold(from.name) && !sema_.isKnownFacet(from.name)) return true;
        if (!sema_.isKnownMold(to.name) && !sema_.isKnownFacet(to.name)) return true;
        return false;
    }
    return false;
}

const MethodDecl* BodyChecker::findMethod(const std::string& startMold, const std::string& methodName,
                                          std::string* owner) const {
    for (auto& moldName : sema_.getMoldChain(startMold)) {
        const MoldDecl* decl = sema_.getMold(moldName);
        if (!decl) continue;
        for (auto& member : decl->members) {
            if (auto* m = std::get_if<MethodDecl>(&member)) {
                if (m->name == methodName) {
                    if (owner) *owner = moldName;
                    return m;
                }
            }
        }
    }
    return nullptr;
}

const FieldDecl* BodyChecker::findField(const std::string& startMold, const std::string& fieldName,
                                        std::string* owner) const {
    for (auto& moldName : sema_.getMoldChain(startMold)) {
        const MoldDecl* decl = sema_.getMold(moldName);
        if (!decl) continue;
        for (auto& member : decl->members) {
            if (auto* f = std::get_if<FieldDecl>(&member)) {
                if (f->name == fieldName) {
                    if (owner) *owner = moldName;
                    return f;
                }
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------
// Модификаторы доступа
// ---------------------------------------------------------------------

bool BodyChecker::isMemberAccessible(AccessModifier access, const std::string& owner) const {
    switch (access) {
        case AccessModifier::Private:
            // currentMoldName_ пуст вне mold'а (свободная функция, инициализатор поля) — owner никогда не пуст.
            return currentMoldName_ == owner;
        case AccessModifier::Protected:
            // Цепочка текущего mold'а — он сам и его предки: owner среди них, значит текущий mold — потомок владельца.
            for (auto& moldName : sema_.getMoldChain(currentMoldName_)) {
                if (moldName == owner) return true;
            }
            return false;
        case AccessModifier::None:
        case AccessModifier::Public:
            return true;
    }
    return true;
}

bool BodyChecker::checkMemberAccess(AccessModifier access, const std::string& owner, const std::string& member,
                                    MemberKind kind, int line, int col) {
    if (isMemberAccessible(access, owner)) return true;
    const bool isPrivate = access == AccessModifier::Private;
    const std::string modifier = isPrivate ? "private" : "protected";
    const std::string only = isPrivate ? "только из самого '" + owner + "'" : "только из '" + owner + "' и его потомков";
    std::string message;
    switch (kind) {
        case MemberKind::Field:
            message = "поле '" + member + "' объявлено " + modifier + " в mold'е '" + owner + "': " +
                      (isPrivate ? "обращаться к нему можно " + only : "доступ " + only);
            break;
        case MemberKind::Method:
            message = "метод '" + member + "' объявлен " + modifier + " в mold'е '" + owner + "': " +
                      (isPrivate ? "вызывать его можно " + only : "доступ " + only);
            break;
        case MemberKind::Constructor:
            message = "конструктор mold'а '" + owner + "' объявлен " + modifier + ": создавать объекты можно " + only;
            break;
    }
    error(line, col, message);
    return false;
}

const FacetMethodSig* BodyChecker::findFacetMethodSig(const std::string& facetName,
                                                       const std::string& methodName) const {
    const FacetDecl* decl = sema_.getFacet(facetName);
    if (!decl) return nullptr;
    for (auto& sig : decl->methods) {
        if (sig.name == methodName) return &sig;
    }
    for (auto& base : decl->baseTypes) {
        if (auto* found = findFacetMethodSig(base, methodName)) return found;
    }
    return nullptr;
}

// ---------------------------------------------------------------------
// Вызовы: функция vs конструкция mold'а vs метод
// ---------------------------------------------------------------------

void BodyChecker::checkArgsAgainstParams(const std::vector<Param>& params, CallExpr& call) {
    if (call.args.size() != params.size()) {
        error(call.line, call.column,
              "неверное число аргументов: ожидалось " + std::to_string(params.size()) + ", передано " +
                  std::to_string(call.args.size()));
        for (auto& a : call.args) checkExpr(*a);
        return;
    }
    for (size_t i = 0; i < params.size(); i++) {
        checkExprAgainstExpected(*call.args[i], params[i].type);
    }
}

TypeNode BodyChecker::checkFunctionCall(const FunctionDecl& fn, CallExpr& call) {
    checkArgsAgainstParams(fn.params, call);
    return fn.returnType ? *fn.returnType : voidType();
}

TypeNode BodyChecker::checkConstruction(const MoldDecl& mold, CallExpr& call) {
    const ConstructorDecl* ctor = nullptr;
    for (auto& m : mold.members) {
        if (auto* c = std::get_if<ConstructorDecl>(&m)) ctor = c;
    }
    if (ctor) {
        checkMemberAccess(ctor->access, mold.name, mold.name, MemberKind::Constructor, call.line, call.column);
        checkArgsAgainstParams(ctor->params, call);
    } else if (!call.args.empty()) {
        error(call.line, call.column,
              "у mold '" + mold.name + "' нет объявленного конструктора, принимающего аргументы");
        for (auto& a : call.args) checkExpr(*a);
    }
    return named(mold.name);
}

TypeNode BodyChecker::checkMethodCallOnMold(const std::string& moldName, const std::string& methodName,
                                             CallExpr& call) {
    std::string owner;
    const MethodDecl* m = findMethod(moldName, methodName, &owner);
    if (!m) {
        error(call.line, call.column, "у mold '" + moldName + "' нет метода '" + methodName + "'");
        for (auto& a : call.args) checkExpr(*a);
        return errorType();
    }
    checkMemberAccess(m->access, owner, methodName, MemberKind::Method, call.line, call.column);
    checkArgsAgainstParams(m->params, call);
    return m->returnType ? *m->returnType : voidType();
}

TypeNode BodyChecker::checkMethodCallOnFacet(const std::string& facetName, const std::string& methodName,
                                              CallExpr& call) {
    const FacetMethodSig* sig = findFacetMethodSig(facetName, methodName);
    if (!sig) {
        error(call.line, call.column, "у facet '" + facetName + "' нет метода '" + methodName + "'");
        for (auto& a : call.args) checkExpr(*a);
        return errorType();
    }
    checkArgsAgainstParams(sig->params, call);
    return sig->returnType ? *sig->returnType : voidType();
}

// ---------------------------------------------------------------------
// Встроенные функции и методы массивов/строк (таблица — bytecode/Builtins.h)
// ---------------------------------------------------------------------

bool BodyChecker::checkArgCount(CallExpr& e, const std::string& what, size_t lo, size_t hi) {
    size_t n = e.args.size();
    if (n >= lo && n <= hi) return true;
    std::string expected = lo == hi ? argWord(lo) : "от " + std::to_string(lo) + " до " + std::to_string(hi) + " аргументов";
    error(e.line, e.column, "'" + what + "' ожидает " + expected + ", передано " + std::to_string(n));
    for (auto& a : e.args) checkExpr(*a);
    return false;
}

TypeNode BodyChecker::checkBuiltinFunction(CallExpr& e, const BuiltinDef& def) {
    const std::string name = def.name;
    switch (def.id) {
        case Builtin::Len: {
            if (!checkArgCount(e, name, 1, 1)) return errorType();
            TypeNode t = checkExpr(*e.args[0]);
            if (isErrT(t)) return errorType();
            if (isArrayT(t) || isStringT(t)) return intType();
            error(e.args[0]->line, e.args[0]->column,
                  "len() ожидает массив или строку, получено '" + typeToString(t) + "'");
            return errorType();
        }
        case Builtin::Str: {
            if (!checkArgCount(e, name, 1, 1)) return stringType();
            TypeNode t = checkExpr(*e.args[0]);
            if (isVoidT(t)) {
                error(e.args[0]->line, e.args[0]->column, "нельзя преобразовать в строку значение типа void");
            }
            return stringType();
        }
        case Builtin::ToInt:
        case Builtin::ToDouble:
        case Builtin::ToBool: {
            TypeNode result = def.id == Builtin::ToInt ? intType() : def.id == Builtin::ToDouble ? doubleType() : boolType();
            if (!checkArgCount(e, name, 1, 1)) return result;
            TypeNode t = checkExpr(*e.args[0]);
            if (!isErrT(t) && !isConvertible(t)) {
                error(e.args[0]->line, e.args[0]->column,
                      name + "() принимает int, double, bool или string, получено '" + typeToString(t) + "'");
            }
            return result;
        }
        case Builtin::Range: {
            if (!checkArgCount(e, name, 1, 3)) return arrayOf(intType());
            for (auto& a : e.args) checkExprAgainstExpected(*a, intType());
            return arrayOf(intType());
        }
        case Builtin::Abs: {
            if (!checkArgCount(e, name, 1, 1)) return errorType();
            TypeNode t = checkExpr(*e.args[0]);
            if (isErrT(t)) return errorType();
            if (!isNumeric(t)) {
                error(e.args[0]->line, e.args[0]->column, "abs() ожидает число, получено '" + typeToString(t) + "'");
                return errorType();
            }
            return t;
        }
        case Builtin::Min:
        case Builtin::Max: {
            if (!checkArgCount(e, name, 1, 2)) return errorType();
            if (e.args.size() == 2) {
                TypeNode a = checkExpr(*e.args[0]);
                TypeNode b = checkExpr(*e.args[1]);
                if (isErrT(a) || isErrT(b)) return errorType();
                if (!isNumeric(a) || !isNumeric(b)) {
                    error(e.line, e.column, name + "() с двумя аргументами ожидает числа, получено '" +
                                                typeToString(a) + "' и '" + typeToString(b) + "'");
                    return errorType();
                }
                if (isDoubleT(a) || isDoubleT(b)) {
                    if (isIntT(a)) e.args[0]->convertToDouble = true;
                    if (isIntT(b)) e.args[1]->convertToDouble = true;
                    return doubleType();
                }
                return intType();
            }
            TypeNode t = checkExpr(*e.args[0]);
            if (isErrT(t)) return errorType();
            if (isArrayT(t) && isNumeric(*t.elementType)) return *t.elementType;
            error(e.args[0]->line, e.args[0]->column,
                  name + "() с одним аргументом ожидает массив чисел, получено '" + typeToString(t) + "'");
            return errorType();
        }
        case Builtin::Sum: {
            if (!checkArgCount(e, name, 1, 1)) return errorType();
            TypeNode t = checkExpr(*e.args[0]);
            if (isErrT(t)) return errorType();
            if (isArrayT(t) && isNumeric(*t.elementType)) {
                if (isDoubleT(*t.elementType)) e.convertToDouble = true;  // sum пустого [double] — 0.0, а не 0
                return *t.elementType;
            }
            error(e.args[0]->line, e.args[0]->column, "sum() ожидает массив чисел, получено '" + typeToString(t) + "'");
            return errorType();
        }
        case Builtin::Sqrt:
        case Builtin::Floor:
        case Builtin::Ceil:
        case Builtin::Round: {
            TypeNode result = def.id == Builtin::Sqrt ? doubleType() : intType();
            if (!checkArgCount(e, name, 1, 1)) return result;
            TypeNode t = checkExpr(*e.args[0]);
            if (!isErrT(t) && !isNumeric(t)) {
                error(e.args[0]->line, e.args[0]->column, name + "() ожидает число, получено '" + typeToString(t) + "'");
            }
            return result;
        }
        case Builtin::Pow: {
            if (!checkArgCount(e, name, 2, 2)) return doubleType();
            for (auto& a : e.args) {
                TypeNode t = checkExpr(*a);
                if (!isErrT(t) && !isNumeric(t)) {
                    error(a->line, a->column, "pow() ожидает числа, получено '" + typeToString(t) + "'");
                }
            }
            return doubleType();
        }
        default:
            break;
    }
    error(e.line, e.column, "встроенная функция '" + name + "' не поддерживается");
    for (auto& a : e.args) checkExpr(*a);
    return errorType();
}

TypeNode BodyChecker::checkArrayMethod(CallExpr& e, FieldAccessExpr& f, const TypeNode& arr) {
    const BuiltinDef* def = findArrayMethod(f.name);
    if (!def) {
        std::string hint = f.name == "length" ? " (длина — свойство, без скобок: xs.length или len(xs))" : "";
        error(e.line, e.column,
              "у массива нет метода '" + f.name + "'" + hint +
                  "; доступны: append, pop, insert, remove, contains, indexOf, reverse, sort, clear, join");
        for (auto& a : e.args) checkExpr(*a);
        return errorType();
    }
    const TypeNode elem = *arr.elementType;
    switch (def->id) {
        case Builtin::ArrAppend:
            if (!checkArgCount(e, f.name, 1, 1)) return voidType();
            checkExprAgainstExpected(*e.args[0], elem);
            return voidType();
        case Builtin::ArrPop:
            if (!checkArgCount(e, f.name, 0, 1)) return elem;
            if (e.args.size() == 1) checkExprAgainstExpected(*e.args[0], intType());
            return elem;
        case Builtin::ArrInsert:
            if (!checkArgCount(e, f.name, 2, 2)) return voidType();
            checkExprAgainstExpected(*e.args[0], intType());
            checkExprAgainstExpected(*e.args[1], elem);
            return voidType();
        case Builtin::ArrRemove:
            if (!checkArgCount(e, f.name, 1, 1)) return voidType();
            checkExprAgainstExpected(*e.args[0], elem);
            return voidType();
        case Builtin::ArrContains:
        case Builtin::ArrIndexOf:
            if (!checkArgCount(e, f.name, 1, 1)) return def->id == Builtin::ArrContains ? boolType() : intType();
            checkExprAgainstExpected(*e.args[0], elem);
            return def->id == Builtin::ArrContains ? boolType() : intType();
        case Builtin::ArrReverse:
        case Builtin::ArrClear:
            checkArgCount(e, f.name, 0, 0);
            return voidType();
        case Builtin::ArrSort:
            checkArgCount(e, f.name, 0, 0);
            if (!isErrT(elem) && !isIntT(elem) && !isDoubleT(elem) && !isStringT(elem)) {
                error(e.line, e.column, "sort() умеет сортировать массивы int, double и string, а не '" +
                                            typeToString(arr) + "'");
            }
            return voidType();
        case Builtin::ArrJoin:
            if (!checkArgCount(e, f.name, 1, 1)) return stringType();
            checkExprAgainstExpected(*e.args[0], stringType());
            return stringType();
        default:
            break;
    }
    return errorType();
}

TypeNode BodyChecker::checkStringMethod(CallExpr& e, FieldAccessExpr& f) {
    const BuiltinDef* def = findStringMethod(f.name);
    if (!def) {
        std::string hint = f.name == "length" ? " (длина — свойство, без скобок: s.length или len(s))" : "";
        error(e.line, e.column,
              "у строки нет метода '" + f.name + "'" + hint +
                  "; доступны: upper, lower, trim, contains, startsWith, endsWith, indexOf, replace, split, repeat");
        for (auto& a : e.args) checkExpr(*a);
        return errorType();
    }
    switch (def->id) {
        case Builtin::StrUpper:
        case Builtin::StrLower:
        case Builtin::StrTrim:
            checkArgCount(e, f.name, 0, 0);
            return stringType();
        case Builtin::StrContains:
        case Builtin::StrStartsWith:
        case Builtin::StrEndsWith:
            if (!checkArgCount(e, f.name, 1, 1)) return boolType();
            checkExprAgainstExpected(*e.args[0], stringType());
            return boolType();
        case Builtin::StrIndexOf:
            if (!checkArgCount(e, f.name, 1, 1)) return intType();
            checkExprAgainstExpected(*e.args[0], stringType());
            return intType();
        case Builtin::StrReplace:
            if (!checkArgCount(e, f.name, 2, 2)) return stringType();
            checkExprAgainstExpected(*e.args[0], stringType());
            checkExprAgainstExpected(*e.args[1], stringType());
            return stringType();
        case Builtin::StrSplit:
            if (!checkArgCount(e, f.name, 1, 1)) return arrayOf(stringType());
            checkExprAgainstExpected(*e.args[0], stringType());
            return arrayOf(stringType());
        case Builtin::StrRepeat:
            if (!checkArgCount(e, f.name, 1, 1)) return stringType();
            checkExprAgainstExpected(*e.args[0], intType());
            return stringType();
        default:
            break;
    }
    return errorType();
}

// ---------------------------------------------------------------------
// ExprVisitor
// ---------------------------------------------------------------------

void BodyChecker::visit(IntLiteralExpr&) { lastType_ = intType(); }
void BodyChecker::visit(DoubleLiteralExpr&) { lastType_ = doubleType(); }
void BodyChecker::visit(BoolLiteralExpr&) { lastType_ = boolType(); }
void BodyChecker::visit(StringLiteralExpr&) { lastType_ = stringType(); }
void BodyChecker::visit(NullLiteralExpr&) { lastType_ = nullType(); }

void BodyChecker::visit(ThisExpr& e) {
    if (currentMoldName_.empty()) {
        error(e.line, e.column, "'this' используется вне метода/конструктора mold'а");
        lastType_ = errorType();
        return;
    }
    lastType_ = named(currentMoldName_);
}

void BodyChecker::visit(SuperExpr& e) {
    if (currentMoldName_.empty()) {
        error(e.line, e.column, "'super' используется вне метода/конструктора mold'а");
        lastType_ = errorType();
        return;
    }
    auto chain = sema_.getMoldChain(currentMoldName_);
    if (chain.size() < 2) {
        error(e.line, e.column, "у mold '" + currentMoldName_ + "' нет базового mold'а — 'super' недоступен");
        for (auto& a : e.args) checkExpr(*a);
        lastType_ = errorType();
        return;
    }
    const std::string& baseName = chain[1];
    if (e.methodName.empty()) {
        const MoldDecl* base = sema_.getMold(baseName);
        const ConstructorDecl* baseCtor = nullptr;
        if (base) {
            for (auto& m : base->members) {
                if (auto* c = std::get_if<ConstructorDecl>(&m)) baseCtor = c;
            }
        }
        if (baseCtor) {
            checkMemberAccess(baseCtor->access, baseName, baseName, MemberKind::Constructor, e.line, e.column);
            if (e.args.size() != baseCtor->params.size()) {
                error(e.line, e.column,
                      "super(...) — неверное число аргументов: ожидалось " +
                          std::to_string(baseCtor->params.size()) + ", передано " + std::to_string(e.args.size()));
                for (auto& a : e.args) checkExpr(*a);
            } else {
                for (size_t i = 0; i < e.args.size(); i++) {
                    checkExprAgainstExpected(*e.args[i], baseCtor->params[i].type);
                }
            }
        } else {
            if (!e.args.empty()) {
                error(e.line, e.column, "у базового mold'а '" + baseName + "' нет конструктора, принимающего аргументы");
            }
            for (auto& a : e.args) checkExpr(*a);
        }
        lastType_ = voidType();
    } else {
        std::string owner;
        const MethodDecl* m = findMethod(baseName, e.methodName, &owner);
        if (!m) {
            error(e.line, e.column, "у базового mold'а '" + baseName + "' нет метода '" + e.methodName + "'");
            for (auto& a : e.args) checkExpr(*a);
            lastType_ = errorType();
            return;
        }
        checkMemberAccess(m->access, owner, e.methodName, MemberKind::Method, e.line, e.column);
        if (e.args.size() != m->params.size()) {
            error(e.line, e.column,
                  "неверное число аргументов: ожидалось " + std::to_string(m->params.size()) + ", передано " +
                      std::to_string(e.args.size()));
            for (auto& a : e.args) checkExpr(*a);
        } else {
            for (size_t i = 0; i < e.args.size(); i++) {
                checkExprAgainstExpected(*e.args[i], m->params[i].type);
            }
        }
        lastType_ = m->returnType ? *m->returnType : voidType();
    }
}

void BodyChecker::visit(IdentifierExpr& e) {
    if (auto v = lookupVar(e.name)) {
        lastType_ = *v;
        return;
    }
    if (!currentMoldName_.empty()) {
        std::string owner;
        if (const FieldDecl* f = findField(currentMoldName_, e.name, &owner)) {
            // private-поле предка по простому имени из потомка тоже недоступно.
            checkMemberAccess(f->access, owner, e.name, MemberKind::Field, e.line, e.column);
            lastType_ = f->type;
            return;
        }
    }
    if (e.name == "PI" || e.name == "E") {   // встроенные константы (двойной точности)
        lastType_ = doubleType();
        return;
    }
    error(e.line, e.column, "неизвестный идентификатор '" + e.name + "'");
    lastType_ = errorType();
}

void BodyChecker::visit(ArrayLiteralExpr& e) {
    if (e.elements.empty()) {
        error(e.line, e.column,
              "невозможно определить тип пустого литерала массива без контекста (например, let x: [T] = [])");
        lastType_ = errorType();
        return;
    }
    TypeNode common = checkExpr(*e.elements[0]);
    for (size_t i = 1; i < e.elements.size(); i++) {
        TypeNode t = checkExpr(*e.elements[i]);
        if (isAssignable(t, common)) continue;
        if (isAssignable(common, t)) {
            common = t;  // [1, 2.5]: общий тип — double
            continue;
        }
        error(e.elements[i]->line, e.elements[i]->column,
              "элемент массива типа '" + typeToString(t) + "' не согласуется с типом предыдущих элементов '" +
                  typeToString(common) + "'");
    }
    if (isDoubleT(common)) {
        for (auto& el : e.elements) {
            if (isIntT(el->resolvedType)) el->convertToDouble = true;
        }
    }
    lastType_ = arrayOf(common);
}

void BodyChecker::visit(GlobalIntrospectExpr& e) {
    lastType_ = (e.kind == GlobalIntrospectKind::Heap) ? named("HeapSnapshot") : voidType();
}

void BodyChecker::visit(UnaryExpr& e) {
    TypeNode t = checkExpr(*e.operand);
    if (e.op == UnaryOp::Not) {
        if (t.name != "bool" && t.name != "<error>") {
            error(e.line, e.column, "'!' применим только к bool, получено '" + typeToString(t) + "'");
        }
        lastType_ = boolType();
    } else {
        if (!isNumeric(t) && t.name != "<error>") {
            error(e.line, e.column, "унарный '-' применим только к числам, получено '" + typeToString(t) + "'");
            lastType_ = errorType();
        } else {
            lastType_ = t;
        }
    }
}

void BodyChecker::visit(CastExpr& e) {
    TypeNode from = checkExpr(*e.operand);
    if (!isKnownType(e.targetType)) {
        error(e.line, e.column, "неизвестный тип приведения '" + typeToString(e.targetType) + "'");
        lastType_ = errorType();
        return;
    }
    // Примитивы: настоящее преобразование ((int) 3.7 == 3, (double) 1 == 1.0, (string) 5 == "5").
    // Ссылочные типы: сам объект проверяет VM во время выполнения (OpCode::CheckCast — по реальному
    // классу), а здесь отсекается то, что не может сработать никогда: число/строка/массив в mold и
    // два mold'а, не связанных наследованием ((Cat) dog).
    if (e.targetType.kind == TypeNode::Kind::Named && !isErrT(from)) {
        const bool fromIsReference = from.kind == TypeNode::Kind::Named || from.name == "<null>";
        if (!fromIsReference) {
            error(e.line, e.column,
                  "нельзя привести '" + typeToString(from) + "' к '" + typeToString(e.targetType) + "'");
        } else if (from.name != "<null>" && sema_.isKnownMold(from.name) && sema_.isKnownMold(e.targetType.name) &&
                   !isAssignable(from, e.targetType) && !isAssignable(e.targetType, from)) {
            error(e.line, e.column,
                  "приведение невозможно: '" + typeToString(from) + "' и '" + typeToString(e.targetType) +
                      "' не связаны наследованием");
        }
    }
    if (e.targetType.kind == TypeNode::Kind::Primitive && !isErrT(from)) {
        if (e.targetType.name == "string") {
            if (isVoidT(from)) error(e.line, e.column, "нельзя преобразовать в строку значение типа void");
        } else if (!isConvertible(from)) {
            error(e.line, e.column,
                  "нельзя привести '" + typeToString(from) + "' к '" + typeToString(e.targetType) + "'");
        }
    }
    lastType_ = e.targetType;
}

void BodyChecker::visit(BinaryExpr& e) {
    TypeNode l = checkExpr(*e.left);
    TypeNode r = checkExpr(*e.right);
    bool lErr = (l.name == "<error>");
    bool rErr = (r.name == "<error>");

    switch (e.op) {
        case BinaryOp::Add:
            if (isStringT(l) || isStringT(r)) {
                // "x = " + 5, 3 + " шт.", "a" + "b": любое значение склеивается со строкой
                // (число, bool, массив, объект — как в print).
                if ((isVoidT(l) || isVoidT(r)) && !lErr && !rErr) {
                    error(e.line, e.column, "нельзя склеить строку со значением типа void");
                }
                lastType_ = stringType();
                return;
            }
            if (isArrayT(l) && isArrayT(r) && (isAssignable(l, r) || isAssignable(r, l))) {
                lastType_ = l;  // [1, 2] + [3] — новый массив
                return;
            }
            if (isNumeric(l) && isNumeric(r)) {
                lastType_ = (l.name == "double" || r.name == "double") ? doubleType() : intType();
                return;
            }
            if (!lErr && !rErr) {
                error(e.line, e.column,
                      "оператор '+' не определён для '" + typeToString(l) + "' и '" + typeToString(r) + "'");
            }
            lastType_ = errorType();
            return;
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
            if (e.op == BinaryOp::Mul && ((isStringT(l) && isIntT(r)) || (isIntT(l) && isStringT(r)))) {
                lastType_ = stringType();  // "ab" * 3 == "ababab"
                return;
            }
            if (isNumeric(l) && isNumeric(r)) {
                lastType_ = (l.name == "double" || r.name == "double") ? doubleType() : intType();
                return;
            }
            if (!lErr && !rErr) {
                error(e.line, e.column,
                      "оператор '" + binaryOpToString(e.op) + "' требует числовые операнды, получено '" +
                          typeToString(l) + "' и '" + typeToString(r) + "'");
            }
            lastType_ = errorType();
            return;
        case BinaryOp::Eq:
        case BinaryOp::NotEq:
            if (!lErr && !rErr && !isAssignable(l, r) && !isAssignable(r, l)) {
                error(e.line, e.column,
                      "'" + binaryOpToString(e.op) + "' сравнивает несовместимые типы '" + typeToString(l) +
                          "' и '" + typeToString(r) + "'");
            }
            lastType_ = boolType();
            return;
        case BinaryOp::Less:
        case BinaryOp::Greater:
        case BinaryOp::LessEq:
        case BinaryOp::GreaterEq:
            if (isStringT(l) && isStringT(r)) {  // лексикографически, по символам
                lastType_ = boolType();
                return;
            }
            if (!isNumeric(l) && !lErr) {
                error(e.line, e.column, "оператор '" + binaryOpToString(e.op) + "' требует числовые операнды");
            } else if (!isNumeric(r) && !rErr) {
                error(e.line, e.column, "оператор '" + binaryOpToString(e.op) + "' требует числовые операнды");
            }
            lastType_ = boolType();
            return;
        case BinaryOp::And:
        case BinaryOp::Or:
            if (l.name != "bool" && !lErr) {
                error(e.line, e.column, "'" + binaryOpToString(e.op) + "' требует bool операнды");
            } else if (r.name != "bool" && !rErr) {
                error(e.line, e.column, "'" + binaryOpToString(e.op) + "' требует bool операнды");
            }
            lastType_ = boolType();
            return;
    }
}

void BodyChecker::visit(AssignExpr& e) {
    TypeNode targetType = checkExpr(*e.target);
    TypeNode valueType = e.op == AssignOp::Assign ? checkExprAgainstExpected(*e.value, targetType) : checkExpr(*e.value);
    if (e.op != AssignOp::Assign) {
        bool numericOk = isNumeric(targetType) && isNumeric(valueType) && isAssignable(valueType, targetType);
        bool stringOk = e.op == AssignOp::AddAssign && isStringT(targetType) && !isVoidT(valueType);  // s += 5
        bool repeatOk = e.op == AssignOp::MulAssign && isStringT(targetType) && isIntT(valueType);   // s *= 3
        bool arrayOk = e.op == AssignOp::AddAssign && isArrayT(targetType) && isArrayT(valueType) &&
                       (isAssignable(valueType, targetType) || isAssignable(targetType, valueType));
        if (!numericOk && !stringOk && !repeatOk && !arrayOk && !isErrT(targetType) && !isErrT(valueType)) {
            error(e.line, e.column,
                  "оператор '" + assignOpToString(e.op) + "' не определён для '" + typeToString(targetType) + "' и '" +
                      typeToString(valueType) + "'");
        }
    }
    lastType_ = targetType;
}

void BodyChecker::visit(CallExpr& e) {
    if (auto* idExpr = dynamic_cast<IdentifierExpr*>(e.callee.get())) {
        const std::string& name = idExpr->name;

        if (name == "print") {
            // print(a, b, c): любое число аргументов любых типов, через пробел; print() — пустая строка.
            for (auto& a : e.args) {
                TypeNode t = checkExpr(*a);
                if (isVoidT(t)) error(a->line, a->column, "нельзя напечатать значение типа void");
            }
            lastType_ = voidType();
            return;
        }

        bool isLocal = lookupVar(name).has_value();
        // Имя закрывает только ДОСТУПНОЕ поле: private-поле предка из потомка не видно и функцию не перекрывает.
        bool isFieldName = false;
        if (!currentMoldName_.empty()) {
            std::string fieldOwner;
            if (const FieldDecl* f = findField(currentMoldName_, name, &fieldOwner)) {
                isFieldName = isMemberAccessible(f->access, fieldOwner);
            }
        }
        if (isLocal || isFieldName) {
            std::string hint = findBuiltinFunction(name) ? "; имя закрыто вашей переменной — переименуйте её, чтобы вызвать встроенную функцию" : "";
            error(e.line, e.column,
                  "'" + name + "' — переменная, а не функция; вызов значений не поддерживается (glass-grammar.md §10)" + hint);
            for (auto& a : e.args) checkExpr(*a);
            lastType_ = errorType();
            return;
        }
        if (const FunctionDecl* fn = sema_.getFunction(name)) {
            lastType_ = checkFunctionCall(*fn, e);
            return;
        }
        if (const MoldDecl* mold = sema_.getMold(name)) {
            lastType_ = checkConstruction(*mold, e);
            return;
        }
        if (const BuiltinDef* def = findBuiltinFunction(name)) {
            lastType_ = checkBuiltinFunction(e, *def);   // свои функции и mold'ы перекрывают встроенные
            return;
        }
        // Метод текущего mold'а (или его предка), вызванный без this.: подменяем вызов на this.имя(...) — дальше он
        // проверяется и компилируется как обычный вызов метода (в том числе виртуальный). Только для имён, которые
        // не нашлись выше: локальная переменная, поле, функция, mold и встроенная функция метод НЕ перекрывает —
        // иначе mold Stack с методом len() не смог бы вызвать встроенную len(items). Нужен именно метод — this.len().
        // static-методы не трогаем: у них нет this, а их вызов пока не поддерживается (TODO.md).
        if (!currentMoldName_.empty()) {
            const MethodDecl* method = findMethod(currentMoldName_, name);
            if (method && method->modifier != MethodModifier::Static) {
                auto self = std::make_unique<ThisExpr>();
                self->line = idExpr->line;
                self->column = idExpr->column;
                auto call = std::make_unique<FieldAccessExpr>();
                call->line = idExpr->line;
                call->column = idExpr->column;
                call->target = std::move(self);
                call->name = name;          // копия: name ссылается на idExpr, который исчезнет со следующей строкой
                e.callee = std::move(call);
                visit(e);                   // теперь callee — FieldAccessExpr: идём обычным путём this.метод(...)
                return;
            }
        }
        error(e.line, e.column, "неизвестная функция или mold '" + name + "'");
        for (auto& a : e.args) checkExpr(*a);
        lastType_ = errorType();
        return;
    }

    if (auto* fieldExpr = dynamic_cast<FieldAccessExpr*>(e.callee.get())) {
        TypeNode targetType = checkExpr(*fieldExpr->target);
        if (isArrayT(targetType)) {
            lastType_ = checkArrayMethod(e, *fieldExpr, targetType);
            return;
        }
        if (isStringT(targetType)) {
            lastType_ = checkStringMethod(e, *fieldExpr);
            return;
        }
        if (targetType.kind == TypeNode::Kind::Named && sema_.isKnownMold(targetType.name)) {
            lastType_ = checkMethodCallOnMold(targetType.name, fieldExpr->name, e);
            return;
        }
        if (targetType.kind == TypeNode::Kind::Named && sema_.isKnownFacet(targetType.name)) {
            lastType_ = checkMethodCallOnFacet(targetType.name, fieldExpr->name, e);
            return;
        }
        if (targetType.kind == TypeNode::Kind::Primitive && !isErrT(targetType)) {
            error(e.line, e.column, "у значений типа '" + typeToString(targetType) + "' нет методов ('." + fieldExpr->name + "()')");
        }
        // Непрозрачный/неизвестный тип (см. класс-комментарий) — молча пропускаем.
        for (auto& a : e.args) checkExpr(*a);
        lastType_ = errorType();
        return;
    }

    error(e.line, e.column, "это выражение не может быть вызвано");
    for (auto& a : e.args) checkExpr(*a);
    lastType_ = errorType();
}

void BodyChecker::visit(FieldAccessExpr& e) {
    TypeNode targetType = checkExpr(*e.target);
    if (isArrayT(targetType) || isStringT(targetType)) {
        if (e.name == "length") {
            lastType_ = intType();
            return;
        }
        error(e.line, e.column, std::string(isStringT(targetType) ? "у строки" : "у массива") + " нет поля '" + e.name +
                                    "' (есть только length; методы вызываются со скобками)");
        lastType_ = errorType();
        return;
    }
    if (targetType.kind == TypeNode::Kind::Named && sema_.isKnownMold(targetType.name)) {
        std::string owner;
        if (const FieldDecl* f = findField(targetType.name, e.name, &owner)) {
            checkMemberAccess(f->access, owner, e.name, MemberKind::Field, e.line, e.column);
            lastType_ = f->type;
            return;
        }
        error(e.line, e.column, "у mold '" + targetType.name + "' нет поля '" + e.name + "'");
        lastType_ = errorType();
        return;
    }
    if (targetType.kind == TypeNode::Kind::Primitive && !isErrT(targetType)) {
        error(e.line, e.column, "у значений типа '" + typeToString(targetType) + "' нет полей ('." + e.name + "')");
    }
    // Непрозрачный/неизвестный тип — молча пропускаем (см. класс-комментарий).
    lastType_ = errorType();
}

void BodyChecker::visit(IndexExpr& e) {
    TypeNode targetType = checkExpr(*e.target);
    TypeNode indexType = checkExpr(*e.index);
    if (indexType.name != "int" && !isErrT(indexType)) {
        error(e.index->line, e.index->column, "индекс должен быть 'int', получено '" + typeToString(indexType) + "'");
    }
    if (isArrayT(targetType)) {
        lastType_ = *targetType.elementType;
        return;
    }
    if (isStringT(targetType)) {
        lastType_ = stringType();  // s[i] — строка из одного символа
        return;
    }
    if (!isErrT(targetType)) {
        error(e.line, e.column, "тип '" + typeToString(targetType) + "' не поддерживает индексацию");
    }
    lastType_ = errorType();
}

void BodyChecker::visit(SliceExpr& e) {
    TypeNode targetType = checkExpr(*e.target);
    for (Expr* part : {e.start.get(), e.stop.get(), e.step.get()}) {
        if (!part) continue;
        TypeNode t = checkExpr(*part);
        if (!isIntT(t) && !isErrT(t)) {
            error(part->line, part->column, "границы и шаг среза должны быть 'int', получено '" + typeToString(t) + "'");
        }
    }
    if (isArrayT(targetType) || isStringT(targetType)) {
        lastType_ = targetType;  // срез массива — массив того же типа, срез строки — строка
        return;
    }
    if (!isErrT(targetType)) {
        error(e.line, e.column, "тип '" + typeToString(targetType) + "' не поддерживает срезы");
    }
    lastType_ = errorType();
}

void BodyChecker::visit(IntrospectFieldExpr& e) {
    TypeNode targetType = checkExpr(*e.target);
    if (targetType.kind != TypeNode::Kind::Named && targetType.name != "<error>") {
        error(e.line, e.column,
              "интроспекция (~" + introspectKindToString(e.kind) + ") применима только к объектам mold, получено '" +
                  typeToString(targetType) + "'");
    }
    switch (e.kind) {
        case IntrospectKind::Layout:
            lastType_ = named("LayoutInfo");
            return;
        case IntrospectKind::Vtable:
            lastType_ = named("VTableInfo");
            return;
        case IntrospectKind::Address:
            lastType_ = intType();
            return;
    }
}

// ---------------------------------------------------------------------
// StmtVisitor
// ---------------------------------------------------------------------

void BodyChecker::visit(LetStmt& s) {
    if (s.type) {
        if (!isKnownType(*s.type)) {
            error(s.line, s.column, "неизвестный тип '" + typeToString(*s.type) + "'");
        }
        if (s.initExpr) {
            checkExprAgainstExpected(*s.initExpr, *s.type);
        }
        defineVar(s.name, *s.type, s.line, s.column);
    } else {
        TypeNode inferred = s.initExpr ? checkExpr(*s.initExpr) : errorType();
        if (inferred.name == "void") {
            error(s.line, s.column, "нельзя объявить переменную с типом, выведенным из выражения без значения");
            inferred = errorType();
        }
        defineVar(s.name, inferred, s.line, s.column);
    }
}

void BodyChecker::visit(BlockStmt& s) {
    pushScope();
    for (auto& st : s.statements) checkStmt(*st);
    popScope();
}

void BodyChecker::visit(IfStmt& s) {
    TypeNode condType = checkExpr(*s.condition);
    if (condType.name != "bool" && condType.name != "<error>") {
        error(s.condition->line, s.condition->column,
              "условие 'if' должно быть bool, получено '" + typeToString(condType) + "'");
    }
    checkStmt(*s.thenBranch);
    if (s.elseBranch) checkStmt(*s.elseBranch);
}

void BodyChecker::visit(WhileStmt& s) {
    TypeNode condType = checkExpr(*s.condition);
    if (condType.name != "bool" && condType.name != "<error>") {
        error(s.condition->line, s.condition->column,
              "условие 'while' должно быть bool, получено '" + typeToString(condType) + "'");
    }
    loopDepth_++;
    checkStmt(*s.body);
    loopDepth_--;
}

void BodyChecker::visit(ForStmt& s) {
    pushScope();
    if (s.rangeEnd) {
        TypeNode a = checkExpr(*s.iterable);
        TypeNode b = checkExpr(*s.rangeEnd);
        if (a.name != "int" && a.name != "<error>") {
            error(s.iterable->line, s.iterable->column, "начало диапазона должно быть 'int'");
        }
        if (b.name != "int" && b.name != "<error>") {
            error(s.rangeEnd->line, s.rangeEnd->column, "конец диапазона должен быть 'int'");
        }
        defineVar(s.varName, intType(), s.line, s.column);
    } else {
        TypeNode iterType = checkExpr(*s.iterable);
        if (iterType.kind == TypeNode::Kind::Array && iterType.elementType) {
            defineVar(s.varName, *iterType.elementType, s.line, s.column);
        } else if (isStringT(iterType)) {
            defineVar(s.varName, stringType(), s.line, s.column);  // for c in "abc": по символам
        } else {
            if (iterType.name != "<error>") {
                error(s.iterable->line, s.iterable->column,
                      "'for..in' ожидает массив, строку или диапазон, получено '" + typeToString(iterType) + "'");
            }
            defineVar(s.varName, errorType(), s.line, s.column);
        }
    }
    loopDepth_++;
    checkStmt(*s.body);
    loopDepth_--;
    popScope();
}

void BodyChecker::visit(ReturnStmt& s) {
    TypeNode expected = currentReturnType_ ? *currentReturnType_ : voidType();
    if (s.value) {
        checkExprAgainstExpected(*s.value, expected);
    } else if (expected.name != "void") {
        error(s.line, s.column, "функция должна вернуть значение типа '" + typeToString(expected) + "'");
    }
}

void BodyChecker::visit(BreakStmt& s) {
    if (loopDepth_ == 0) error(s.line, s.column, "'break' вне цикла");
}

void BodyChecker::visit(ContinueStmt& s) {
    if (loopDepth_ == 0) error(s.line, s.column, "'continue' вне цикла");
}

void BodyChecker::visit(TryStmt& s) {
    checkStmt(*s.tryBlock);
    for (auto& c : s.catches) {
        pushScope();
        // Тип исключения намеренно не проверяется на существование —
        // см. класс-комментарий про 'Exception' и стандартную библиотеку.
        defineVar(c.varName, c.type, c.body->line, c.body->column);
        checkStmt(*c.body);
        popScope();
    }
    if (s.finallyBlock) checkStmt(*s.finallyBlock);
}

void BodyChecker::visit(ThrowStmt& s) { checkExpr(*s.value); }

void BodyChecker::visit(ExprStmt& s) { checkExpr(*s.expr); }

void BodyChecker::visit(EmptyStmt&) {}

void BodyChecker::checkAllPathsReturn(const std::string& what, const std::optional<TypeNode>& returnType,
                                      const BlockStmt& body, int line, int col) {
    if (!returnType || isVoidT(*returnType)) return;  // ничего не возвращает — нечего требовать
    if (alwaysExits(body)) return;
    error(line, col,
          "не все ветки " + what + " возвращают значение типа " + typeToString(*returnType) +
              ": после последнего оператора выполнение может дойти до конца без return");
}

// ---------------------------------------------------------------------
// Точка входа: обход всех деклараций
// ---------------------------------------------------------------------

std::vector<std::string> BodyChecker::checkProgram(Program& program) {
    errors_.clear();

    for (auto& decl : program.declarations) {
        if (auto* mold = std::get_if<MoldDecl>(&decl)) {
            for (auto& member : mold->members) {
                if (auto* field = std::get_if<FieldDecl>(&member)) {
                    if (field->initExpr) {
                        currentMoldName_.clear();
                        currentGenericScope_ = mold->genericParams;
                        currentReturnType_.reset();
                        scopeStack_.clear();
                        pushScope();
                        checkExprAgainstExpected(*field->initExpr, field->type);
                        popScope();
                    }
                } else if (auto* method = std::get_if<MethodDecl>(&member)) {
                    currentMoldName_ = mold->name;
                    currentGenericScope_ = mold->genericParams;
                    for (auto& g : method->genericParams) currentGenericScope_.push_back(g);
                    currentReturnType_ = method->returnType;
                    scopeStack_.clear();
                    pushScope();
                    for (auto& p : method->params) defineVar(p.name, p.type, p.line, p.column);
                    checkStmt(*method->body);
                    popScope();
                    checkAllPathsReturn("метода '" + method->name + "' mold'а '" + mold->name + "'", method->returnType,
                                        *method->body, method->line, method->column);
                } else if (auto* ctor = std::get_if<ConstructorDecl>(&member)) {
                    currentMoldName_ = mold->name;
                    currentGenericScope_ = mold->genericParams;
                    currentReturnType_.reset();
                    scopeStack_.clear();
                    pushScope();
                    for (auto& p : ctor->params) defineVar(p.name, p.type, p.line, p.column);
                    checkStmt(*ctor->body);
                    popScope();
                }
            }
            currentMoldName_.clear();
        } else if (auto* fn = std::get_if<FunctionDecl>(&decl)) {
            currentMoldName_.clear();
            currentGenericScope_ = fn->genericParams;
            currentReturnType_ = fn->returnType;
            scopeStack_.clear();
            pushScope();
            for (auto& p : fn->params) defineVar(p.name, p.type, p.line, p.column);
            checkStmt(*fn->body);
            popScope();
            checkAllPathsReturn("функции '" + fn->name + "'", fn->returnType, *fn->body, fn->line, fn->column);
        }
    }

    currentReturnType_.reset();
    return errors_;
}

} // namespace glass

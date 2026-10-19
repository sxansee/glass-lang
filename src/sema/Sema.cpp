#include "Sema.h"

#include <functional>
#include <unordered_set>

#include "../ast/Type.h"

namespace glass {

size_t Sema::SigKeyHash::operator()(const SigKey& k) const {
    size_t h = std::hash<std::string>{}(k.name);
    auto mix = [&h](const std::string& s) {
        h ^= std::hash<std::string>{}(s) + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    for (auto& p : k.paramTypes) mix(p);
    mix(k.returnType);
    return h;
}

void Sema::error(int line, int col, const std::string& message) {
    errors_.push_back(std::to_string(line) + ":" + std::to_string(col) + ": " + message);
}

std::vector<std::string> Sema::analyze(Program& program) {
    molds_.clear();
    facets_.clear();
    functions_.clear();
    errors_.clear();

    collectDeclarations(program);
    resolveInheritance();
    checkMoldCycles();
    checkFacetCycles();
    checkTypesInSignatures();
    checkMoldMembers();
    checkFacetContracts();
    checkEntryPoint();

    return errors_;
}

void Sema::collectDeclarations(Program& program) {
    auto nameTaken = [&](const std::string& n) {
        return molds_.count(n) || facets_.count(n) || functions_.count(n);
    };
    for (auto& decl : program.declarations) {
        if (auto* m = std::get_if<MoldDecl>(&decl)) {
            if (nameTaken(m->name)) {
                error(m->line, m->column, "повторное объявление имени '" + m->name + "'");
                continue;
            }
            molds_[m->name] = MoldInfo{m, "", {}};
        } else if (auto* f = std::get_if<FacetDecl>(&decl)) {
            if (nameTaken(f->name)) {
                error(f->line, f->column, "повторное объявление имени '" + f->name + "'");
                continue;
            }
            facets_[f->name] = FacetInfo{f};
        } else if (auto* fn = std::get_if<FunctionDecl>(&decl)) {
            if (nameTaken(fn->name)) {
                error(fn->line, fn->column, "повторное объявление имени '" + fn->name + "'");
                continue;
            }
            functions_[fn->name] = FunctionInfo{fn};
        }
    }
}

void Sema::resolveInheritance() {
    for (auto& [name, info] : molds_) {
        int moldBases = 0;
        for (auto& baseName : info.decl->baseTypes) {
            if (molds_.count(baseName)) {
                moldBases++;
                if (moldBases == 1) {
                    info.baseMoldName = baseName;
                } else {
                    error(info.decl->line, info.decl->column,
                          "mold '" + name + "' указывает больше одного базового mold'а ('" +
                              baseName + "' лишний — наследовать можно только один)");
                }
            } else if (facets_.count(baseName)) {
                info.facetNames.push_back(baseName);
            } else {
                error(info.decl->line, info.decl->column,
                      "неизвестный базовый тип '" + baseName + "' у mold '" + name + "'");
            }
        }
    }
    for (auto& [name, info] : facets_) {
        for (auto& baseName : info.decl->baseTypes) {
            if (molds_.count(baseName)) {
                error(info.decl->line, info.decl->column,
                      "facet '" + name + "' не может наследовать mold ('" + baseName +
                          "') — только другие facet'ы");
            } else if (!facets_.count(baseName)) {
                error(info.decl->line, info.decl->column,
                      "неизвестный базовый facet '" + baseName + "' у facet '" + name + "'");
            }
        }
    }
}

void Sema::checkMoldCycles() {
    enum class State { Unvisited, InProgress, Done };
    std::unordered_map<std::string, State> state;
    for (auto& [name, info] : molds_) state[name] = State::Unvisited;

    std::function<bool(const std::string&)> visit = [&](const std::string& name) -> bool {
        auto it = molds_.find(name);
        if (it == molds_.end()) return false;
        if (state[name] == State::Done) return false;
        if (state[name] == State::InProgress) return true;
        state[name] = State::InProgress;
        if (!it->second.baseMoldName.empty() && visit(it->second.baseMoldName)) {
            state[name] = State::Done;
            return true;
        }
        state[name] = State::Done;
        return false;
    };

    for (auto& [name, info] : molds_) {
        if (state[name] == State::Unvisited && visit(name)) {
            error(info.decl->line, info.decl->column,
                  "цикл в цепочке наследования mold'ов, начиная с '" + name + "'");
        }
    }
}

void Sema::checkFacetCycles() {
    enum class State { Unvisited, InProgress, Done };
    std::unordered_map<std::string, State> state;
    for (auto& [name, info] : facets_) state[name] = State::Unvisited;

    std::function<bool(const std::string&)> visit = [&](const std::string& name) -> bool {
        auto it = facets_.find(name);
        if (it == facets_.end()) return false;
        if (state[name] == State::Done) return false;
        if (state[name] == State::InProgress) return true;
        state[name] = State::InProgress;
        for (auto& base : it->second.decl->baseTypes) {
            if (facets_.count(base) && visit(base)) {
                state[name] = State::Done;
                return true;
            }
        }
        state[name] = State::Done;
        return false;
    };

    for (auto& [name, info] : facets_) {
        if (state[name] == State::Unvisited && visit(name)) {
            error(info.decl->line, info.decl->column,
                  "цикл в цепочке наследования facet'ов, начиная с '" + name + "'");
        }
    }
}

bool Sema::isKnownTypeName(const std::string& name, const std::vector<std::string>& genericScope) const {
    if (molds_.count(name) || facets_.count(name)) return true;
    for (auto& g : genericScope)
        if (g == name) return true;
    return false;
}

void Sema::checkType(const TypeNode& t, const std::vector<std::string>& genericScope) {
    if (t.kind == TypeNode::Kind::Primitive) return;
    if (t.kind == TypeNode::Kind::Array) {
        if (t.elementType) checkType(*t.elementType, genericScope);
        return;
    }
    if (!isKnownTypeName(t.name, genericScope)) {
        error(t.line, t.column, "неизвестный тип '" + t.name + "'");
    }
    for (auto& arg : t.genericArgs) checkType(arg, genericScope);
}

void Sema::checkTypesInSignatures() {
    for (auto& [name, info] : molds_) {
        auto scope = info.decl->genericParams;
        for (auto& member : info.decl->members) {
            if (auto* field = std::get_if<FieldDecl>(&member)) {
                checkType(field->type, scope);
            } else if (auto* method = std::get_if<MethodDecl>(&member)) {
                auto methodScope = scope;
                for (auto& g : method->genericParams) methodScope.push_back(g);
                for (auto& p : method->params) checkType(p.type, methodScope);
                if (method->returnType) checkType(*method->returnType, methodScope);
            } else if (auto* ctor = std::get_if<ConstructorDecl>(&member)) {
                for (auto& p : ctor->params) checkType(p.type, scope);
            }
        }
    }
    for (auto& [name, info] : facets_) {
        auto scope = info.decl->genericParams;
        for (auto& sig : info.decl->methods) {
            for (auto& p : sig.params) checkType(p.type, scope);
            if (sig.returnType) checkType(*sig.returnType, scope);
        }
    }
    for (auto& [name, info] : functions_) {
        auto scope = info.decl->genericParams;
        for (auto& p : info.decl->params) checkType(p.type, scope);
        if (info.decl->returnType) checkType(*info.decl->returnType, scope);
    }
}

std::vector<std::string> Sema::moldChain(const std::string& moldName) const {
    std::vector<std::string> chain;
    std::unordered_set<std::string> seen;
    std::string cur = moldName;
    while (!cur.empty() && molds_.count(cur) && !seen.count(cur)) {
        seen.insert(cur);
        chain.push_back(cur);
        cur = molds_.at(cur).baseMoldName;
    }
    return chain;
}

Sema::SigKey Sema::sigKeyOf(const std::string& name, const std::vector<Param>& params,
                             const std::optional<TypeNode>& ret) const {
    SigKey k;
    k.name = name;
    for (auto& p : params) k.paramTypes.push_back(typeToString(p.type));
    k.returnType = ret ? typeToString(*ret) : "";
    return k;
}

Sema::SigKey Sema::sigKeyOfFacet(const FacetMethodSig& sig) const {
    return sigKeyOf(sig.name, sig.params, sig.returnType);
}

bool Sema::findMethodInChain(const std::string& startMold, const SigKey& key) const {
    for (auto& moldName : moldChain(startMold)) {
        auto it = molds_.find(moldName);
        if (it == molds_.end()) continue;
        for (auto& member : it->second.decl->members) {
            if (auto* method = std::get_if<MethodDecl>(&member)) {
                if (sigKeyOf(method->name, method->params, method->returnType) == key) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Sema::checkMoldMembers() {
    for (auto& [name, info] : molds_) {
        int ctorCount = 0;
        std::unordered_set<std::string> memberNames;
        for (auto& member : info.decl->members) {
            if (auto* field = std::get_if<FieldDecl>(&member)) {
                if (!memberNames.insert(field->name).second) {
                    error(field->line, field->column,
                          "повторное имя члена '" + field->name + "' в mold '" + name + "'");
                }
            } else if (auto* method = std::get_if<MethodDecl>(&member)) {
                if (!memberNames.insert(method->name).second) {
                    error(method->line, method->column,
                          "повторное имя члена '" + method->name + "' в mold '" + name + "'");
                }
                if (method->name == "toString" && method->modifier != MethodModifier::Static) {
                    // toString — договорённость языка: его вызывают print, '+' со строкой и str(x).
                    const bool returnsString = method->returnType.has_value() &&
                                               method->returnType->kind == TypeNode::Kind::Primitive &&
                                               method->returnType->name == "string";
                    if (!method->params.empty() || !returnsString) {
                        error(method->line, method->column,
                              "метод 'toString' используют print, '+' со строкой и str(x), поэтому он должен быть "
                              "без параметров и возвращать string: fn toString() -> string");
                    }
                }
                if (method->modifier == MethodModifier::Override) {
                    SigKey key = sigKeyOf(method->name, method->params, method->returnType);
                    std::string declaringMold;
                    const MethodDecl* baseMethod =
                        info.baseMoldName.empty() ? nullptr : findMethodDeclInChain(info.baseMoldName, key, &declaringMold);
                    if (!baseMethod) {
                        error(method->line, method->column,
                              "метод '" + method->name +
                                  "' помечен override, но в базовом mold'е нет метода с такой же сигнатурой");
                    } else if (!methodIsVirtual(declaringMold, *baseMethod)) {
                        // Без virtual в базе метод вызывается по статическому типу переменной, а не по
                        // реальному классу объекта — override в наследнике молча не сработал бы.
                        error(method->line, method->column,
                              "метод '" + method->name + "' помечен override, но в mold'е '" + declaringMold +
                                  "' одноимённый метод не virtual — добавьте virtual (или override) в '" +
                                  declaringMold + "', иначе вызов через тип '" + declaringMold +
                                  "' не дойдёт до этого метода");
                    }
                } else if (method->modifier == MethodModifier::None && !info.baseMoldName.empty()) {
                    // Метод с той же сигнатурой, что у virtual-метода базы, но без override: в vtable он
                    // не попадает и остаётся мёртвым кодом — виртуальные вызовы идут в базовую версию.
                    // (Обычное «сокрытие» над НЕ virtual методом допустимо: вызов идёт по статическому типу.)
                    SigKey key = sigKeyOf(method->name, method->params, method->returnType);
                    std::string declaringMold;
                    const MethodDecl* baseMethod = findMethodDeclInChain(info.baseMoldName, key, &declaringMold);
                    if (baseMethod && methodIsVirtual(declaringMold, *baseMethod) && !methodIsVirtual(name, *method)) {
                        error(method->line, method->column,
                              "метод '" + method->name + "' совпадает по сигнатуре с virtual-методом mold'а '" +
                                  declaringMold + "' — добавьте override, иначе он не попадёт в vtable и не будет вызываться");
                    }
                }
            } else if (auto* ctor = std::get_if<ConstructorDecl>(&member)) {
                ctorCount++;
                if (ctorCount > 1) {
                    error(ctor->line, ctor->column,
                          "mold '" + name +
                              "' может иметь только один конструктор (перегрузка конструкторов не поддерживается)");
                }
                if (ctor->name != name) {
                    error(ctor->line, ctor->column,
                          "имя конструктора '" + ctor->name + "' не совпадает с именем mold'а '" + name + "'");
                }
            }
        }
    }
}

void Sema::checkFacetContracts() {
    for (auto& [name, info] : molds_) {
        for (auto& facetName : info.facetNames) {
            if (!facets_.count(facetName)) continue;  // уже отдельная ошибка

            std::unordered_set<std::string> allFacets;
            std::function<void(const std::string&)> collect = [&](const std::string& fn) {
                if (allFacets.count(fn)) return;
                allFacets.insert(fn);
                auto it = facets_.find(fn);
                if (it == facets_.end()) return;
                for (auto& base : it->second.decl->baseTypes) collect(base);
            };
            collect(facetName);

            for (auto& fn : allFacets) {
                auto it = facets_.find(fn);
                if (it == facets_.end()) continue;
                for (auto& sig : it->second.decl->methods) {
                    SigKey key = sigKeyOfFacet(sig);
                    if (!findMethodInChain(name, key)) {
                        error(info.decl->line, info.decl->column,
                              "mold '" + name + "' реализует facet '" + facetName +
                                  "', но не предоставляет метод '" + sig.name + "' с ожидаемой сигнатурой");
                    }
                }
            }
        }
    }
}

void Sema::checkEntryPoint() {
    auto it = functions_.find("main");
    if (it == functions_.end()) {
        errors_.push_back("0:0: не найдена точка входа: нужна функция верхнего уровня 'fn main()'");
        return;
    }
    auto* fn = it->second.decl;
    if (!fn->params.empty()) {
        error(fn->line, fn->column, "'main' не должна принимать параметры");
    }
    if (fn->returnType.has_value()) {
        error(fn->line, fn->column, "'main' не должна иметь возвращаемый тип (-> Type)");
    }
}

const MoldDecl* Sema::getMold(const std::string& name) const {
    auto it = molds_.find(name);
    return it != molds_.end() ? it->second.decl : nullptr;
}

const FacetDecl* Sema::getFacet(const std::string& name) const {
    auto it = facets_.find(name);
    return it != facets_.end() ? it->second.decl : nullptr;
}

const FunctionDecl* Sema::getFunction(const std::string& name) const {
    auto it = functions_.find(name);
    return it != functions_.end() ? it->second.decl : nullptr;
}

const MethodDecl* Sema::findMethodDeclInChain(const std::string& startMold, const SigKey& key,
                                              std::string* declaringMold) const {
    for (auto& moldName : moldChain(startMold)) {
        auto it = molds_.find(moldName);
        if (it == molds_.end()) continue;
        for (auto& member : it->second.decl->members) {
            if (auto* method = std::get_if<MethodDecl>(&member)) {
                if (sigKeyOf(method->name, method->params, method->returnType) == key) {
                    if (declaringMold) *declaringMold = moldName;
                    return method;
                }
            }
        }
    }
    return nullptr;
}

bool Sema::facetDeclaresSignature(const std::string& facetName, const SigKey& key,
                                  std::unordered_set<std::string>& seen) const {
    if (!seen.insert(facetName).second) return false;  // защита от цикла facet'ов (он ловится отдельно)
    auto it = facets_.find(facetName);
    if (it == facets_.end() || !it->second.decl) return false;
    for (auto& sig : it->second.decl->methods) {
        if (sigKeyOfFacet(sig) == key) return true;
    }
    for (auto& base : it->second.decl->baseTypes) {
        if (facetDeclaresSignature(base, key, seen)) return true;
    }
    return false;
}

bool Sema::methodIsVirtual(const std::string& moldName, const MethodDecl& method) const {
    if (method.modifier == MethodModifier::Static) return false;
    if (method.modifier == MethodModifier::Virtual || method.modifier == MethodModifier::Override) return true;
    if (method.name == "toString" && method.params.empty()) return true;  // неявно виртуальный
    auto it = molds_.find(moldName);
    if (it == molds_.end()) return false;
    SigKey key = sigKeyOf(method.name, method.params, method.returnType);
    for (auto& facetName : it->second.facetNames) {
        std::unordered_set<std::string> seen;
        if (facetDeclaresSignature(facetName, key, seen)) return true;  // реализация facet-контракта
    }
    return false;
}

const std::vector<std::string>& Sema::getMoldFacets(const std::string& name) const {
    static const std::vector<std::string> empty;
    auto it = molds_.find(name);
    return it != molds_.end() ? it->second.facetNames : empty;
}

} // namespace glass

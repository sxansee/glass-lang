#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../ast/Decl.h"

namespace glass {

// Семантический анализ уровня деклараций и сигнатур: символы, разрешение
// mold/facet в базовых списках, циклы наследования, facet-контракты,
// проверка override, соответствие имени конструктора имени mold'а, типы
// в полях/параметрах/возвращаемых значениях, точка входа main.
//
// НЕ спускается в тела функций/методов — проверка операторов и выражений
// внутри тел (typechecker в полном смысле) — следующая фаза, работающая
// поверх символьной таблицы, которую строит этот проход.
class Sema {
public:
    // Возвращает список ошибок ("line:col: сообщение"). Пустой — анализ
    // прошёл чисто на уровне деклараций.
    std::vector<std::string> analyze(Program& program);

    // Публичный доступ к уже разрешённой таблице символов — нужен
    // BodyChecker'у (проверка тел функций/методов), который работает
    // поверх результатов этого прохода.
    bool isKnownMold(const std::string& name) const { return molds_.count(name) > 0; }
    bool isKnownFacet(const std::string& name) const { return facets_.count(name) > 0; }
    bool isKnownFunction(const std::string& name) const { return functions_.count(name) > 0; }
    const MoldDecl* getMold(const std::string& name) const;
    const FacetDecl* getFacet(const std::string& name) const;
    const FunctionDecl* getFunction(const std::string& name) const;
    std::vector<std::string> getMoldChain(const std::string& name) const { return moldChain(name); }
    const std::vector<std::string>& getMoldFacets(const std::string& name) const;

private:
    struct MoldInfo {
        MoldDecl* decl = nullptr;
        std::string baseMoldName;         // пусто = базовый — неявный корневой Glass
        std::vector<std::string> facetNames;
    };
    struct FacetInfo {
        FacetDecl* decl = nullptr;
    };
    struct FunctionInfo {
        FunctionDecl* decl = nullptr;
    };

    struct SigKey {
        std::string name;
        std::vector<std::string> paramTypes;
        std::string returnType;  // "" — функция/метод ничего не возвращает
        bool operator==(const SigKey& o) const {
            return name == o.name && paramTypes == o.paramTypes && returnType == o.returnType;
        }
    };
    struct SigKeyHash {
        size_t operator()(const SigKey& k) const;
    };

    std::unordered_map<std::string, MoldInfo> molds_;
    std::unordered_map<std::string, FacetInfo> facets_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::vector<std::string> errors_;

    void error(int line, int col, const std::string& message);

    void collectDeclarations(Program& program);
    void resolveInheritance();
    void checkMoldCycles();
    void checkFacetCycles();
    void checkTypesInSignatures();
    void checkMoldMembers();
    void checkFacetContracts();
    void checkEntryPoint();

    bool isKnownTypeName(const std::string& name, const std::vector<std::string>& genericScope) const;
    void checkType(const TypeNode& t, const std::vector<std::string>& genericScope);

    std::vector<std::string> moldChain(const std::string& moldName) const;

    SigKey sigKeyOf(const std::string& name, const std::vector<Param>& params,
                     const std::optional<TypeNode>& ret) const;
    SigKey sigKeyOfFacet(const FacetMethodSig& sig) const;
    bool findMethodInChain(const std::string& startMold, const SigKey& key) const;

    // Ближайший метод с такой сигнатурой в цепочке startMold → его базы (nullptr — нет такого).
    // declaringMold получает имя mold'а, в котором метод объявлен.
    const MethodDecl* findMethodDeclInChain(const std::string& startMold, const SigKey& key,
                                            std::string* declaringMold) const;

    // facet (или один из его базовых facet'ов) объявляет метод с такой сигнатурой.
    bool facetDeclaresSignature(const std::string& facetName, const SigKey& key,
                                std::unordered_set<std::string>& seen) const;

    // Метод попадает в vtable. Те же правила, что у Compiler::buildClassInfo: помечен virtual/override,
    // это toString() без параметров, либо он реализует контракт facet'а своего mold'а.
    bool methodIsVirtual(const std::string& moldName, const MethodDecl& method) const;
};

} // namespace glass

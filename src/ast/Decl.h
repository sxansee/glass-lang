#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Stmt.h"
#include "Type.h"

namespace glass {

struct Param {
    std::string name;
    TypeNode type;
    int line = 0;
    int column = 0;
};

enum class AccessModifier { None, Public, Private, Protected };
enum class MethodModifier { None, Static, Virtual, Override };

struct FieldDecl {
    AccessModifier access = AccessModifier::None;
    bool isStatic = false;
    std::string name;
    TypeNode type;
    ExprPtr initExpr;  // nullable
    int line = 0;
    int column = 0;
};

struct MethodDecl {
    AccessModifier access = AccessModifier::None;
    MethodModifier modifier = MethodModifier::None;
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<Param> params;
    std::optional<TypeNode> returnType;  // nullopt = функция ничего не возвращает
    std::unique_ptr<BlockStmt> body;
    int line = 0;
    int column = 0;
};

struct ConstructorDecl {
    AccessModifier access = AccessModifier::None;
    std::string name;  // должно совпадать с именем mold'а — проверяется семантически, не грамматикой
    std::vector<Param> params;
    std::unique_ptr<BlockStmt> body;
    int line = 0;
    int column = 0;
};

using MoldMember = std::variant<FieldDecl, MethodDecl, ConstructorDecl>;

struct MoldDecl {
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<std::string> baseTypes;  // после ':' — какой mold, какие facet'ы, решает семантика
    std::vector<MoldMember> members;
    int line = 0;
    int column = 0;
};

struct FacetMethodSig {
    std::string name;
    std::vector<Param> params;
    std::optional<TypeNode> returnType;
    int line = 0;
    int column = 0;
};

struct FacetDecl {
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<std::string> baseTypes;
    std::vector<FacetMethodSig> methods;
    int line = 0;
    int column = 0;
};

struct FunctionDecl {
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<Param> params;
    std::optional<TypeNode> returnType;
    std::unique_ptr<BlockStmt> body;
    int line = 0;
    int column = 0;
};

using TopLevelDecl = std::variant<MoldDecl, FacetDecl, FunctionDecl>;

struct Program {
    std::vector<TopLevelDecl> declarations;
};

} // namespace glass

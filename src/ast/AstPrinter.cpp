#include "AstPrinter.h"

#include <sstream>

namespace glass {

std::string AstPrinter::print(Expr& e) {
    out_.clear();
    e.accept(*this);
    return out_;
}

std::string AstPrinter::print(Stmt& s) {
    out_.clear();
    s.accept(*this);
    return out_;
}

void AstPrinter::visit(IntLiteralExpr& e) { out_ += "(int " + std::to_string(e.value) + ")"; }

void AstPrinter::visit(DoubleLiteralExpr& e) {
    std::ostringstream oss;
    oss << e.value;
    out_ += "(double " + oss.str() + ")";
}

void AstPrinter::visit(BoolLiteralExpr& e) { out_ += e.value ? "(bool true)" : "(bool false)"; }

void AstPrinter::visit(StringLiteralExpr& e) { out_ += "(str \"" + e.value + "\")"; }

void AstPrinter::visit(NullLiteralExpr&) { out_ += "(null)"; }

void AstPrinter::visit(ThisExpr&) { out_ += "(this)"; }

void AstPrinter::visit(SuperExpr& e) {
    out_ += "(super";
    if (!e.methodName.empty()) out_ += " ." + e.methodName;
    for (auto& a : e.args) {
        out_ += " ";
        a->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(IdentifierExpr& e) {
    out_ += "(id " + e.name;
    for (auto& g : e.genericArgs) out_ += " " + typeToString(g);
    out_ += ")";
}

void AstPrinter::visit(ArrayLiteralExpr& e) {
    out_ += "(array";
    for (auto& el : e.elements) {
        out_ += " ";
        el->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(GlobalIntrospectExpr& e) {
    out_ += e.kind == GlobalIntrospectKind::Heap ? "(~heap)" : "(~gc)";
}

void AstPrinter::visit(UnaryExpr& e) {
    out_ += "(unary " + unaryOpToString(e.op) + " ";
    e.operand->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(CastExpr& e) {
    out_ += "(cast " + typeToString(e.targetType) + " ";
    e.operand->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(BinaryExpr& e) {
    out_ += "(binary " + binaryOpToString(e.op) + " ";
    e.left->accept(*this);
    out_ += " ";
    e.right->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(AssignExpr& e) {
    out_ += "(assign " + assignOpToString(e.op) + " ";
    e.target->accept(*this);
    out_ += " ";
    e.value->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(CallExpr& e) {
    out_ += "(call ";
    e.callee->accept(*this);
    for (auto& a : e.args) {
        out_ += " ";
        a->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(FieldAccessExpr& e) {
    out_ += "(field ";
    e.target->accept(*this);
    out_ += " ." + e.name + ")";
}

void AstPrinter::visit(IndexExpr& e) {
    out_ += "(index ";
    e.target->accept(*this);
    out_ += " ";
    e.index->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(SliceExpr& e) {
    out_ += "(slice ";
    e.target->accept(*this);
    for (Expr* part : {e.start.get(), e.stop.get(), e.step.get()}) {
        out_ += " ";
        if (part) part->accept(*this);
        else out_ += "_";
    }
    out_ += ")";
}

void AstPrinter::visit(IntrospectFieldExpr& e) {
    out_ += "(introspect ";
    e.target->accept(*this);
    out_ += " ~" + introspectKindToString(e.kind) + ")";
}

void AstPrinter::visit(LetStmt& s) {
    out_ += "(let " + s.name;
    if (s.type) out_ += ":" + typeToString(*s.type);
    if (s.initExpr) {
        out_ += " = ";
        s.initExpr->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(BlockStmt& s) {
    out_ += "(block";
    for (auto& st : s.statements) {
        out_ += " ";
        st->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(IfStmt& s) {
    out_ += "(if ";
    s.condition->accept(*this);
    out_ += " ";
    s.thenBranch->accept(*this);
    if (s.elseBranch) {
        out_ += " else ";
        s.elseBranch->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(WhileStmt& s) {
    out_ += "(while ";
    s.condition->accept(*this);
    out_ += " ";
    s.body->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(ForStmt& s) {
    out_ += "(for " + s.varName + " in ";
    s.iterable->accept(*this);
    if (s.rangeEnd) {
        out_ += "..";
        s.rangeEnd->accept(*this);
    }
    out_ += " ";
    s.body->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(ReturnStmt& s) {
    out_ += "(return";
    if (s.value) {
        out_ += " ";
        s.value->accept(*this);
    }
    out_ += ")";
}

void AstPrinter::visit(BreakStmt&) { out_ += "(break)"; }

void AstPrinter::visit(ContinueStmt&) { out_ += "(continue)"; }

void AstPrinter::visit(TryStmt& s) {
    out_ += "(try ";
    s.tryBlock->accept(*this);
    for (auto& c : s.catches) {
        out_ += " (catch " + c.varName + ":" + typeToString(c.type) + " ";
        c.body->accept(*this);
        out_ += ")";
    }
    if (s.finallyBlock) {
        out_ += " (finally ";
        s.finallyBlock->accept(*this);
        out_ += ")";
    }
    out_ += ")";
}

void AstPrinter::visit(ThrowStmt& s) {
    out_ += "(throw ";
    s.value->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(ExprStmt& s) {
    out_ += "(exprstmt ";
    s.expr->accept(*this);
    out_ += ")";
}

void AstPrinter::visit(EmptyStmt&) { out_ += "(empty)"; }

namespace {
std::string accessToString(AccessModifier a) {
    switch (a) {
        case AccessModifier::Public: return "public ";
        case AccessModifier::Private: return "private ";
        case AccessModifier::Protected: return "protected ";
        case AccessModifier::None: return "";
    }
    return "";
}

std::string paramsToString(const std::vector<Param>& params) {
    std::string s;
    for (size_t i = 0; i < params.size(); i++) {
        if (i) s += ", ";
        s += params[i].name + ":" + typeToString(params[i].type);
    }
    return s;
}

std::string returnTypeSuffix(const std::optional<TypeNode>& rt) {
    return rt ? (" -> " + typeToString(*rt)) : "";
}
} // namespace

std::string printMoldMember(MoldMember& member) {
    AstPrinter printer;
    return std::visit(
        [&](auto&& m) -> std::string {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, FieldDecl>) {
                std::string s = "(field " + accessToString(m.access);
                if (m.isStatic) s += "static ";
                s += m.name + ":" + typeToString(m.type);
                if (m.initExpr) s += " = " + printer.print(*m.initExpr);
                return s + ")";
            } else if constexpr (std::is_same_v<T, MethodDecl>) {
                std::string mod = m.modifier == MethodModifier::Static     ? "static "
                                   : m.modifier == MethodModifier::Virtual ? "virtual "
                                   : m.modifier == MethodModifier::Override ? "override "
                                                                             : "";
                return "(method " + accessToString(m.access) + mod + m.name + "(" +
                       paramsToString(m.params) + ")" + returnTypeSuffix(m.returnType) + " " +
                       printer.print(*m.body) + ")";
            } else {
                return "(ctor " + accessToString(m.access) + m.name + "(" +
                       paramsToString(m.params) + ") " + printer.print(*m.body) + ")";
            }
        },
        member);
}

std::string printTopLevelDecl(TopLevelDecl& decl) {
    return std::visit(
        [](auto&& d) -> std::string {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, MoldDecl>) {
                std::string s = "(mold " + d.name;
                if (!d.baseTypes.empty()) {
                    s += " :";
                    for (auto& b : d.baseTypes) s += " " + b;
                }
                for (auto& m : d.members) s += " " + printMoldMember(const_cast<MoldMember&>(m));
                return s + ")";
            } else if constexpr (std::is_same_v<T, FacetDecl>) {
                std::string s = "(facet " + d.name;
                if (!d.baseTypes.empty()) {
                    s += " :";
                    for (auto& b : d.baseTypes) s += " " + b;
                }
                for (auto& sig : d.methods) {
                    s += " (sig " + sig.name + "(" + paramsToString(sig.params) + ")" +
                         returnTypeSuffix(sig.returnType) + ")";
                }
                return s + ")";
            } else {
                AstPrinter printer;
                return "(fn " + d.name + "(" + paramsToString(d.params) + ")" +
                       returnTypeSuffix(d.returnType) + " " + printer.print(*d.body) + ")";
            }
        },
        decl);
}

std::string printProgram(Program& program) {
    std::string s;
    for (size_t i = 0; i < program.declarations.size(); i++) {
        if (i) s += "\n";
        s += printTopLevelDecl(program.declarations[i]);
    }
    return s;
}

} // namespace glass

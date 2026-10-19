#include "Expr.h"

namespace glass {

std::string binaryOpToString(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::Eq: return "==";
        case BinaryOp::NotEq: return "!=";
        case BinaryOp::Less: return "<";
        case BinaryOp::Greater: return ">";
        case BinaryOp::LessEq: return "<=";
        case BinaryOp::GreaterEq: return ">=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
    }
    return "?";
}

std::string assignOpToString(AssignOp op) {
    switch (op) {
        case AssignOp::Assign: return "=";
        case AssignOp::AddAssign: return "+=";
        case AssignOp::SubAssign: return "-=";
        case AssignOp::MulAssign: return "*=";
        case AssignOp::DivAssign: return "/=";
    }
    return "?";
}

std::string unaryOpToString(UnaryOp op) {
    return op == UnaryOp::Not ? "!" : "-";
}

std::string introspectKindToString(IntrospectKind kind) {
    switch (kind) {
        case IntrospectKind::Layout: return "layout";
        case IntrospectKind::Vtable: return "vtable";
        case IntrospectKind::Address: return "address";
    }
    return "?";
}

} // namespace glass

#include "Type.h"

namespace glass {

std::string typeToString(const TypeNode& t) {
    switch (t.kind) {
        case TypeNode::Kind::Primitive:
            return t.name;
        case TypeNode::Kind::Array:
            return "[" + (t.elementType ? typeToString(*t.elementType) : std::string("?")) + "]";
        case TypeNode::Kind::Named: {
            std::string s = t.name;
            if (!t.genericArgs.empty()) {
                s += "<";
                for (size_t i = 0; i < t.genericArgs.size(); i++) {
                    if (i) s += ", ";
                    s += typeToString(t.genericArgs[i]);
                }
                s += ">";
            }
            return s;
        }
    }
    return "?";
}

} // namespace glass

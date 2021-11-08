#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>

namespace nix {

// binary operators are implemented in nixexpr.hh MakeBinOp

// https://stackoverflow.com/questions/7724448
// note: we use full jump table to make this as fast as possible
// note: we assume valid input. errors should be handled by the nix parser
// 93 * 7 = 651 byte
const char String_showAsJson_replace_array[93][7] = {
  "\\u0000", "\\u0001", "\\u0002", "\\u0003", "\\u0004", // 0 - 4
  "\\u0005", "\\u0006", "\\u0007", "\\b", "\\t", // 5 - 9
  "\\n", "\\u000b", "\\f", "\\r", "\\u000e", // 10 - 14
  "\\u000f", "\\u0010", "\\u0011", "\\u0012", "\\u0013", // 15 - 19
  "\\u0014", "\\u0015", "\\u0016", "\\u0017", "\\u0018", // 20 - 24
  "\\u0019", "\\u001a", "\\u001b", "\\u001c", "\\u001d", // 25 - 29
  "\\u001e", "\\u001f", " ", "!", "\\\"", // 30 - 34
  "#", "$", "%", "&", "'", // 35 - 39
  "(", ")", "*", "+", ",", // 40 - 44
  "-", ".", "/", "0", "1", // 45 - 49
  "2", "3", "4", "5", "6", // 50 - 54
  "7", "8", "9", ":", ";", // 55 - 59
  "<", "=", ">", "?", "@", // 60 - 64
  "A", "B", "C", "D", "E", // 65 - 69
  "F", "G", "H", "I", "J", // 70 - 74
  "K", "L", "M", "N", "O", // 75 - 79
  "P", "Q", "R", "S", "T", // 80 - 84
  "U", "V", "W", "X", "Y", // 85 - 89
  "Z", "[", "\\\\", // 90 - 92
};

void String_showAsJson(std::ostream & o, const std::string & s) {
  for (auto c = s.cbegin(); c != s.cend(); c++) {
    if ((std::uint8_t) *c <= 92)
      o << String_showAsJson_replace_array[(std::uint8_t) *c];
    else
      o << *c;
  }
}

void Expr::showAsJson(std::ostream & str) const
{
    abort();
}

void ExprInt::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprInt << "\"";
    str << ",\"value\":" << n;
    str << '}';
}

void ExprFloat::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprFloat << "\"";
    str << ",\"value\":" << nf;
    str << '}';
}

void ExprString::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprString << "\"";
    str << ",\"value\":\""; String_showAsJson(str, s); str << "\"";
    str << '}';
}

void ExprPath::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprPath << "\"";
    str << ",\"value\":\""; String_showAsJson(str, s); str << "\"";
    str << '}';
}

void ExprVar::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprVar << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"name\":\""; String_showAsJson(str, name); str << "\"";
    str << '}';
}

void ExprSelect::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprSelect << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"set\":"; e->showAsJson(str);
    str << ",\"attr\":"; AttrPath_showAsJson(str, attrPath);
    if (def) {
        str << ",\"default\":"; def->showAsJson(str);
    }
    str << "}";
}

void ExprOpHasAttr::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprOpHasAttr << "\"";
    str << ",\"set\":"; e->showAsJson(str);
    str << ",\"attr\":"; AttrPath_showAsJson(str, attrPath);
    str << '}';
}

void ExprAttrs::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprAttrs << "\"";
    str << ",\"recursive\":" << (recursive ? "true" : "false");
    str << ",\"attrs\":[";
    bool first = true;
    for (auto & i : attrs) {
        if (first) first = false; else str << ",";
        if (i.second.pos.line > 0) {
            str << "{\"line\":" << i.second.pos.line;
            str << ",\"column\":" << i.second.pos.column << ',';
        }
        else {
            str << '{';
        }
        str << "\"inherited\":" << (i.second.inherited ? "true" : "false"); // NOTE inherited is always false. { inherit (scope) attr; } -> { attr = scope.attr; }
        str << ",\"name\":\""; String_showAsJson(str, i.first); str << "\"";
        if (!i.second.inherited) {
            str << ",\"value\":"; i.second.e->showAsJson(str);
        }
        str << '}';
    }
    str << "]";
    str << ",\"dynamicAttrs\":[";
    first = true;
    for (auto & i : dynamicAttrs) {
        if (first) first = false; else str << ",";
        if (i.pos.line > 0) {
            str << "{\"line\":" << i.pos.line;
            str << ",\"column\":" << i.pos.column << ',';
        }
        else {
            str << '{';
        }
        str << "\"name\":"; i.nameExpr->showAsJson(str);
        str << ",\"value\":"; i.valueExpr->showAsJson(str);
        str << '}';
    }
    str << "]}";
}

void ExprList::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprList << "\"";
    str << ",\"items\":[";
    bool first = true;
    for (auto & i : elems) {
        if (first) first = false; else str << ",";
        i->showAsJson(str);
    }
    str << "]}";
}

void ExprLambda::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprLambda << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"hasFormals\":" << (hasFormals() ? "true" : "false");
    if (hasFormals()) {
        str << ",\"formals\":[";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ",";
            if (i.pos.line > 0) {
                str << "{\"line\":" << i.pos.line;
                str << ",\"column\":" << i.pos.column << ',';
            }
            else {
                str << '{';
            }
            str << "\"name\":\""; String_showAsJson(str, i.name); str << "\"";
            if (i.def) {
                str << ",\"default\":"; i.def->showAsJson(str);
            }
            str << '}';
        }
        str << "]";
        str << ",\"ellipsis\":" << (formals->ellipsis ? "true" : "false");
    }
    if (!arg.empty())
        str << ",\"arg\":\"" << arg << "\"";
    str << ",\"body\":"; body->showAsJson(str);
    str << '}';
}

void ExprCall::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprCall << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"function\":";
    fun->showAsJson(str);
    str << ",\"args\":[";
    bool first = true;
    for (auto & e : args) {
        if (first) first = false; else str << ",";
        e->showAsJson(str);
    }
    str << "]}";
}

void ExprLet::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprLet << "\"";
    str << ",\"attrs\":[";
    bool first = true;
    for (auto & i : attrs->attrs) {
        if (first) first = false; else str << ",";
        str << "{\"inherited\":" << (i.second.inherited ? "true" : "false");
        str << ",\"name\":\""; String_showAsJson(str, i.first); str << "\"";
        if (!i.second.inherited) {
            str << ",\"value\":"; i.second.e->showAsJson(str);
        }
        str << '}';
    }
    str << "]";
    str << ",\"body\":"; body->showAsJson(str);
    str << '}';
}

void ExprWith::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprWith << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"set\":"; attrs->showAsJson(str);
    str << ",\"body\":"; body->showAsJson(str);
    str << '}';
}

void ExprIf::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprIf << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"cond\":"; cond->showAsJson(str);
    str << ",\"then\":"; then->showAsJson(str);
    str << ",\"else\":"; else_->showAsJson(str);
    str << '}';
}

void ExprAssert::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprAssert << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"cond\":"; cond->showAsJson(str);
    str << ",\"body\":"; body->showAsJson(str);
    str << '}';
}

void ExprOpNot::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprOpNot << "\"";
    str << ",\"expr\":"; e->showAsJson(str);
    str << '}';
}

void ExprConcatStrings::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprConcatStrings << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << ",\"strings\":[";
    bool first = true;
    for (auto & i : *es) {
        if (first) first = false; else str << ",";
        i->showAsJson(str);
    }
    str << "]}";
}

void ExprPos::showAsJson(std::ostream & str) const
{
    str << "{\"type\":\"" << NodeTypeName::ExprPos << "\"";
    if (pos.line > 0) {
        str << ",\"line\":" << pos.line;
        str << ",\"column\":" << pos.column;
    }
    str << "}";
}

void AttrPath_showAsJson(std::ostream & out, const AttrPath & attrPath)
{
    out << "[";
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << ','; else first = false;
        out << "{";
        if (i.symbol.set()) {
            out << "\"symbol\":\""; String_showAsJson(out, i.symbol); out << "\"";
        }
        else {
            out << "\"expr\":"; i.expr->showAsJson(out);
        }
        out << "}";
    }
    out << "]";
}

}

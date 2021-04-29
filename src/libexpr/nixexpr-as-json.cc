#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>

namespace nix {

// json output format

// FIXME segfaults on large nix files, e.g. nixpkgs/pkgs/top-level/all-packages.nix -> buffer limit? segfault after exactly 1568768 bytes of output = 1532 * 1024
// -> TODO: compile with `-g` and run in `gdb`
// FIXME:
// ./nix-instantiate --parse --json <( echo null )
// workaround:
// echo '{}: { foo = "bar"; }' >test.nix; ./nix-instantiate --parse --json test.nix | jq
// error: getting status of '/dev/fd/pipe:[15897656]': No such file or directory
// TODO move to separate file?
// TODO remove \n (only for debug)
// TODO support 2 json schemas? json-array (fast) and json-object (verbose)
// TEST valid json:
// make -j $NIX_BUILD_CORES && ./nix-instantiate --parse --json /nix/store/*nix*/*/nixpkgs/pkgs/top-level/all-packages.nix >test.all-packages.nix.json ; cat test.all-packages.nix.json | jq
// TEST simple test for balanced braces:
// for c in '{' '}' '[' ']'; do echo $c $(fgrep -o "$c" test.all-packages.nix.json | wc -l); done

// https://stackoverflow.com/questions/7724448
// note: we use full jump table to make this as fast as possible
// note: we assume valid input. errors should be handled by the nix parser
// 93 * 7 = 651 byte / 4096 page = 16%
char String_showAsJson_replace_array[93][7] = {
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
    str << "{\"type\":" << (int) NodeTypeId::ExprInt;
    str << ",\"value\":" << n;
    str << "}\n";
}

void ExprFloat::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprFloat;
    str << ",\"value\":" << nf;
    str << "}\n";
}

void ExprString::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprString;
    str << ",\"value\":\""; String_showAsJson(str, s); str << "\"";
    str << "}\n";
}

// TODO stop parser from transforming relative to absolute paths
// parsed path should be exactly as declared in the nix file
void ExprPath::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprPath;
    str << ",\"value\":\""; String_showAsJson(str, s); str << "\"";
    str << "}\n";
}

void ExprVar::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprVar;
    str << ",\"name\":\""; String_showAsJson(str, name); str << "\"";
    str << "}\n";
}

void ExprSelect::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprSelect;
    str << ",\"set\":"; e->showAsJson(str);
    str << ",\"attr\":"; AttrPath_showAsJson(str, attrPath);
    if (def) {
        str << ",\"default\":"; def->showAsJson(str);
    }
    str << " }";
}

void ExprOpHasAttr::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprOpHasAttr;
    str << ",\"op\":"; e->showAsJson(str);
    str << ",\"attr\":"; AttrPath_showAsJson(str, attrPath);
    str << "}\n";
}

void ExprAttrs::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprAttrs;
    str << ",\"recursive\":" << (recursive ? "true" : "false");
    str << ",\"attrs\":[";
    bool first = true;
    for (auto & i : attrs) {
        if (first) first = false; else str << ",";
        str << "{\"type\":\"attr\"";
        str << ",\"inherited\":" << (i.second.inherited ? "true" : "false");
        str << ",\"name\":\""; String_showAsJson(str, i.first); str << "\"";
        if (!i.second.inherited) {
            str << ",\"value\":"; i.second.e->showAsJson(str);
        }
        str << "}";
    }
    str << "]";
    str << ",\"dynamicAttrs\":[";
    first = true;
    for (auto & i : dynamicAttrs) {
        if (first) first = false; else str << ",";
        str << "{\"type\":\"attr\"";
        str << ",\"nameExpr\":\""; i.nameExpr->showAsJson(str);
        str << ",\"valueExpr\":"; i.valueExpr->showAsJson(str);
        str << "}";
    }
    str << "]}\n";
}

void ExprList::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprList;
    str << ",\"items\":["; // TODO name? items, elements, values
    bool first = true;
    for (auto & i : elems) {
        if (first) first = false; else str << ",";
        i->showAsJson(str);
    }
    str << "]}";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#Functions
void ExprLambda::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprLambda;
    str << ",\"matchAttrs\":" << (matchAttrs ? "true" : "false");
    if (matchAttrs) {
        str << ",\"formals\":[";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ",";
            str << "{\"type\":" << (int) NodeTypeId::ExprLambdaFormal;
            str << ",\"name\":\""; String_showAsJson(str, i.name); str << "\"";
            if (i.def) {
                str << ",\"default\":"; i.def->showAsJson(str);
            }
            str << "}";
        }
        str << "]";
        str << ",\"ellipsis\":" << (formals->ellipsis ? "true" : "false");
    }
    if (!arg.empty())
        str << ",\"arg\":\"" << arg << "\"";
    str << ",\"body\":"; body->showAsJson(str);
    str << "}\n";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#let_..._in_statement
void ExprLet::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprLet;
    str << ",\"attrs\":[";
    bool first = true;
    for (auto & i : attrs->attrs) {
        if (first) first = false; else str << ",";
        str << "{\"type\":" << (int) NodeTypeId::ExprAttr;
        str << ",\"inherited\":" << (i.second.inherited ? "true" : "false");
        str << ",\"name\":\""; String_showAsJson(str, i.first); str << "\"";
        if (!i.second.inherited) {
            str << ",\"value\":"; i.second.e->showAsJson(str);
        }
        str << "}";
    }
    str << "]";
    str << ",\"body\":"; body->showAsJson(str);
    str << "}\n";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#with_statement
void ExprWith::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprWith;
    str << ",\"set\":"; attrs->showAsJson(str);
    str << ",\"body\":"; body->showAsJson(str);
    str << "}\n";
}

void ExprIf::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprIf;
    str << ",\"cond\":"; cond->showAsJson(str);
    str << ",\"then\":"; then->showAsJson(str);
    str << ",\"else\":"; else_->showAsJson(str);
    str << "}\n";
}

void ExprAssert::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprAssert;
    str << ",\"cond\":"; cond->showAsJson(str);
    str << ",\"body\":"; body->showAsJson(str);
    str << "}\n";
}

void ExprOpNot::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprOpNot;
    str << ",\"expr\":"; e->showAsJson(str);
    str << "}\n";
}

void ExprConcatStrings::showAsJson(std::ostream & str) const
{
    str << "{\"type\":" << (int) NodeTypeId::ExprConcatStrings;
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
    str << "{\"type\":" << (int) NodeTypeId::ExprPos << "}";
}


void AttrPath_showAsJson(std::ostream & out, const AttrPath & attrPath)
{
    out << "{\"type\":" << (int) NodeTypeId::ExprAttrPath;
    out << ",\"attrpath\":[";
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << ','; else first = false;
        out << "{\"type\":" << (int) NodeTypeId::ExprAttrPathComponent;
        if (i.symbol.set()) {
            // symbol can contain double quotes (etc?)
            out << ",\"symbol\":\""; String_showAsJson(out, i.symbol); out << "\"";
        }
        else {
            out << ",\"expr\":"; i.expr->showAsJson(out);
        }
        out << "}";
    }
    out << "]}";
}

}

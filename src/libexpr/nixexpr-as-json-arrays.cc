#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>

// debug
#include <iostream>

namespace nix {

// json output format

// binary operators are implemented in nixexpr.hh MakeBinOp

// TODO reduce number of types

// TODO `json-arrays` format
// = positional json schema
// = scalar attributes first (type id, name), complex attributes last (body, expr)

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
char String_showAsJsonArrays_replace_array[93][7] = {
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

void String_showAsJsonArrays(std::ostream & o, const std::string & s) {
  for (auto c = s.cbegin(); c != s.cend(); c++) {
    if ((std::uint8_t) *c <= 92)
      o << String_showAsJsonArrays_replace_array[(std::uint8_t) *c];
    else
      o << *c;
  }
}

void Expr::showAsJsonArrays(std::ostream & str) const
{
    abort();
}

void ExprInt::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprInt;
    str << ',' << n;
    str << ']';
}

void ExprFloat::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprFloat;
    str << ',' << nf;
    str << ']';
}

void ExprString::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprString;
    str << ",\""; String_showAsJsonArrays(str, s); str << '"';
    str << ']';
}

// TODO stop parser from transforming relative to absolute paths
// parsed path should be exactly as declared in the nix file
void ExprPath::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprPath;
    str << ",\""; String_showAsJsonArrays(str, s); str << '"';
    str << ']';
}

void ExprVar::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprVar;
    str << ",\""; String_showAsJsonArrays(str, name); str << '"';
    str << ']';
}

void ExprSelect::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprSelect;
    str << ','; e->showAsJsonArrays(str);
    str << ','; AttrPath_showAsJsonArrays(str, attrPath);
    if (def) {
        str << ','; def->showAsJsonArrays(str);
    }
    str << ']';
}

void ExprOpHasAttr::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprOpHasAttr;
    str << ','; e->showAsJsonArrays(str);
    str << ','; AttrPath_showAsJsonArrays(str, attrPath);
    str << ']';
}

void ExprAttrs::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprAttrs;
    str << ',' << (recursive ? '1' : '0');
    str << ",[";
    bool first = true;
    for (auto & i : attrs) {
        if (first) first = false; else str << ',';
        str << '[' << (i.second.inherited ? '1' : '0');
        str << ",\""; String_showAsJsonArrays(str, i.first); str << '"';
        if (!i.second.inherited) {
            str << ','; i.second.e->showAsJsonArrays(str);
        }
        str << ']';
    }
    str << ']';
    str << ",[";
    first = true;
    for (auto & i : dynamicAttrs) {
        if (first) first = false; else str << ',';
        str << '[';
        str << ",\""; i.nameExpr->showAsJsonArrays(str); str << '"';
        str << ','; i.valueExpr->showAsJsonArrays(str);
        str << ']';
    }
    str << "]]";
}

void ExprList::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprList;
    str << ",[";
    bool first = true;
    for (auto & i : elems) {
        if (first) first = false; else str << ',';
        i->showAsJsonArrays(str);
    }
    str << "]]";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#Functions
void ExprLambda::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprLambda;
    str << ',' << (matchAttrs ? '1' : '0');

    if (matchAttrs) {
        str << ",[";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ',';
            str << '[';
            str << "\""; String_showAsJsonArrays(str, i.name); str << '"';
            if (i.def) {
                str << ','; i.def->showAsJsonArrays(str);
            }
            str << ']';
        }
        str << ']';
        str << ',' << (formals->ellipsis ? '1' : '0');
    }
    else {
        str << ",0,0";
    }

    if (!arg.empty()) {
        str << ",\"" << arg << '"'; // FIXME this causes segfault
        ////////////////////str << ",\"" << "foo_arg" << '"';
        // input: (lambda_arg: null)
    }
    else {
        str << ",0";
    }

    /* showAsJson:
    if (!arg.empty())
        str << ",\"arg\":\"" << arg << "\"";
    */
    // body is last index

    // debug
    /*
    std::cout << "<<< lambda body = ";
    body->showAsJsonArrays(std::cout); // #include <iostream>
    std::cout << " >>>";
    */

    str << ','; body->showAsJsonArrays(str); // FIXME segfault
    str << ']';
}

// https://nixos.wiki/wiki/Nix_Expression_Language#let_..._in_statement
void ExprLet::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprLet;
    str << ",[";
    bool first = true;
    for (auto & i : attrs->attrs) {
        if (first) first = false; else str << ',';
        str << '[' << (i.second.inherited ? '1' : '0');
        str << ",\""; String_showAsJsonArrays(str, i.first); str << '"';
        if (!i.second.inherited) {
            str << ','; i.second.e->showAsJsonArrays(str);
        }
        str << ']';
    }
    str << ']';
    str << ','; body->showAsJsonArrays(str);
    str << ']';
}

// https://nixos.wiki/wiki/Nix_Expression_Language#with_statement
void ExprWith::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprWith;
    str << ','; attrs->showAsJsonArrays(str);
    str << ','; body->showAsJsonArrays(str);
    str << ']';
}

void ExprIf::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprIf;
    str << ','; cond->showAsJsonArrays(str);
    str << ','; then->showAsJsonArrays(str);
    str << ','; else_->showAsJsonArrays(str);
    str << ']';
}

void ExprAssert::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprAssert;
    str << ','; cond->showAsJsonArrays(str);
    str << ','; body->showAsJsonArrays(str);
    str << ']';
}

void ExprOpNot::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprOpNot;
    str << ','; e->showAsJsonArrays(str);
    str << ']';
}

void ExprConcatStrings::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprConcatStrings;
    str << ",[";
    bool first = true;
    for (auto & i : *es) {
        if (first) first = false; else str << ',';
        i->showAsJsonArrays(str);
    }
    str << "]]";
}

void ExprPos::showAsJsonArrays(std::ostream & str) const
{
    str << '[' << (int) NodeTypeId::ExprPos << ']';
}


void AttrPath_showAsJsonArrays(std::ostream & out, const AttrPath & attrPath)
{
    out << '[';
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << ','; else first = false;
        if (i.symbol.set()) {
            // index 0: isExpr
            out << "[0,\""; String_showAsJsonArrays(out, i.symbol); out << "\"]";
        }
        else {
            out << "[1,"; i.expr->showAsJsonArrays(out); out << ']';
        }
    }
    out << ']';
}

}

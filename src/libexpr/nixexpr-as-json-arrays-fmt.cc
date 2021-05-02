#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>

// debug
#include <iostream>

// https://github.com/fmtlib/fmt
#include "include/fmt/include/fmt/core.h"

#include "nixexpr-node-types.h"

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
char String_showAsJsonArraysFmt_replace_array[93][7] = {
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

void String_showAsJsonArraysFmt(std::ostream & o, const std::string & s) {
  for (auto c = s.cbegin(); c != s.cend(); c++) {
    if ((std::uint8_t) *c <= 92)
      o << String_showAsJsonArraysFmt_replace_array[(std::uint8_t) *c];
    else
      o << *c;
  }
}

void Expr::showAsJsonArraysFmt(std::ostream & str) const
{
    abort();
}

void ExprInt::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprInt) "," << n << ']';
}

void ExprFloat::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprFloat) "," << nf << ']';
}

void ExprString::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprString) ",\"";
    String_showAsJsonArraysFmt(str, s);
    str << "\"]";
}

// TODO stop parser from transforming relative to absolute paths
// parsed path should be exactly as declared in the nix file
void ExprPath::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprPath) ",\"";
    String_showAsJsonArraysFmt(str, s);
    str << "\"]";
}

void ExprVar::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprVar) ",\"";
    String_showAsJsonArraysFmt(str, name);
    str << "\"]";
}

void ExprSelect::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprSelect);
    str << ','; e->showAsJsonArraysFmt(str);
    str << ','; AttrPath_showAsJsonArraysFmt(str, attrPath);
    if (def) {
        str << ','; def->showAsJsonArraysFmt(str);
    }
    str << ']';
}

void ExprOpHasAttr::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprOpHasAttr);
    str << ','; e->showAsJsonArraysFmt(str);
    str << ','; AttrPath_showAsJsonArraysFmt(str, attrPath);
    str << ']';
}

void ExprAttrs::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprAttrs);
    str << ',' << (recursive ? '1' : '0');
    str << ",[";
    bool first = true;
    for (auto & i : attrs) {
        if (first) first = false; else str << ',';
        str << '[' << (i.second.inherited ? '1' : '0');
        str << ",\""; String_showAsJsonArraysFmt(str, i.first); str << '"';
        if (!i.second.inherited) {
            str << ','; i.second.e->showAsJsonArraysFmt(str);
        }
        str << ']';
    }
    str << ']';
    str << ",[";
    first = true;
    for (auto & i : dynamicAttrs) {
        if (first) first = false; else str << ',';
        str << '[';
        str << ",\""; i.nameExpr->showAsJsonArraysFmt(str); str << '"';
        str << ','; i.valueExpr->showAsJsonArraysFmt(str);
        str << ']';
    }
    str << "]]";
}

void ExprList::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprList);
    str << ",[";
    bool first = true;
    for (auto & i : elems) {
        if (first) first = false; else str << ',';
        i->showAsJsonArraysFmt(str);
    }
    str << "]]";
}

// https://nixos.wiki/wiki/Nix_Expression_Language#Functions
void ExprLambda::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprLambda);
    str << ',' << (matchAttrs ? '1' : '0');

    if (matchAttrs) {
        str << ",[";
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else str << ',';
            str << '[';
            str << "\""; String_showAsJsonArraysFmt(str, i.name); str << '"';
            if (i.def) {
                str << ','; i.def->showAsJsonArraysFmt(str);
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
    body->showAsJsonArraysFmt(std::cout); // #include <iostream>
    std::cout << " >>>";
    */

    str << ','; body->showAsJsonArraysFmt(str); // FIXME segfault
    str << ']';
}

// https://nixos.wiki/wiki/Nix_Expression_Language#let_..._in_statement
void ExprLet::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprLet);
    str << ",[";
    bool first = true;
    for (auto & i : attrs->attrs) {
        if (first) first = false; else str << ',';
        str << '[' << (i.second.inherited ? '1' : '0');
        str << ",\""; String_showAsJsonArraysFmt(str, i.first); str << '"';
        if (!i.second.inherited) {
            str << ','; i.second.e->showAsJsonArraysFmt(str);
        }
        str << ']';
    }
    str << ']';
    str << ','; body->showAsJsonArraysFmt(str);
    str << ']';
}

// https://nixos.wiki/wiki/Nix_Expression_Language#with_statement
void ExprWith::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprWith);
    str << ','; attrs->showAsJsonArraysFmt(str);
    str << ','; body->showAsJsonArraysFmt(str);
    str << ']';
}

void ExprIf::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprIf);
    str << ','; cond->showAsJsonArraysFmt(str);
    str << ','; then->showAsJsonArraysFmt(str);
    str << ','; else_->showAsJsonArraysFmt(str);
    str << ']';
}

void ExprAssert::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprAssert);
    str << ','; cond->showAsJsonArraysFmt(str);
    str << ','; body->showAsJsonArraysFmt(str);
    str << ']';
}

void ExprOpNot::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprOpNot);
    str << ','; e->showAsJsonArraysFmt(str);
    str << ']';
}

void ExprConcatStrings::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprConcatStrings);
    str << ",[";
    bool first = true;
    for (auto & i : *es) {
        if (first) first = false; else str << ',';
        i->showAsJsonArraysFmt(str);
    }
    str << "]]";
}

void ExprPos::showAsJsonArraysFmt(std::ostream & str) const
{
    str << "[" TYPEIDSTR(ExprPos) "]";
}


void AttrPath_showAsJsonArraysFmt(std::ostream & out, const AttrPath & attrPath)
{
    out << '[';
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << ','; else first = false;
        if (i.symbol.set()) {
            // index 0: isExpr
            out << "[0,\""; String_showAsJsonArraysFmt(out, i.symbol); out << "\"]";
        }
        else {
            out << "[1,"; i.expr->showAsJsonArraysFmt(out); out << ']';
        }
    }
    out << ']';
}

}

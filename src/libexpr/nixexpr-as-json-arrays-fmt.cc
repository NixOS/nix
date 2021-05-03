#include "nixexpr.hh"
#include "derivations.hh"
#include "util.hh"

#include <cstdlib>

// debug
#include <iostream>
#include <sys/select.h>

// https://github.com/fmtlib/fmt
#define FMT_HEADER_ONLY
#include "libfmt/core.h" // libfmt::print

#include "nixexpr-node-types.h" // TYPEIDSTR

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

void String_showAsJsonArraysFmt(FILE *fd, const std::string & s) {
  for (auto c = s.cbegin(); c != s.cend(); c++) {
    if ((std::uint8_t) *c <= 92)
      libfmt::print(fd, "{}", String_showAsJsonArraysFmt_replace_array[(std::uint8_t) *c]);
    else
      libfmt::print(fd, "{}", *c);
  }
}

void Expr::showAsJsonArraysFmt(FILE *fd) const
{
    abort();
}

void ExprInt::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprInt) ",{}]", n);
}

void ExprFloat::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprFloat) ",{}]", nf);
}

void ExprString::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprString) ",\"");
    String_showAsJsonArraysFmt(fd, s);
    libfmt::print(fd, "\"]");
}

// TODO stop parser from transforming relative to absolute paths
// parsed path should be exactly as declared in the nix file
void ExprPath::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprPath) ",\"");
    String_showAsJsonArraysFmt(fd, s);
    libfmt::print(fd, "\"]");
}

void ExprVar::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprVar) ",\"");
    String_showAsJsonArraysFmt(fd, name);
    libfmt::print(fd, "\"]");
}

void ExprSelect::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprSelect) ",");
    e->showAsJsonArraysFmt(fd);
    libfmt::print(fd, ",");
    AttrPath_showAsJsonArraysFmt(fd, attrPath);
    if (def) {
        libfmt::print(fd, ",");
        def->showAsJsonArraysFmt(fd);
    }
    libfmt::print(fd, "]");
}

void ExprOpHasAttr::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprOpHasAttr) ",");
    e->showAsJsonArraysFmt(fd);
    libfmt::print(fd, ",");
    AttrPath_showAsJsonArraysFmt(fd, attrPath);
    libfmt::print(fd, "]");
}

void ExprAttrs::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprAttrs) ",{},[", (recursive ? '1' : '0'));

    bool first = true;
    for (auto & i : attrs) {
        if (first) first = false; else libfmt::print(fd, ",");
        libfmt::print(fd, "[{},\"", (i.second.inherited ? '1' : '0'));
        String_showAsJsonArraysFmt(fd, i.first);
        libfmt::print(fd, "\"");
        if (!i.second.inherited) {
            libfmt::print(fd, ",");
            i.second.e->showAsJsonArraysFmt(fd);
        }
        libfmt::print(fd, "]");
    }
    libfmt::print(fd, "],[");
    first = true;
    for (auto & i : dynamicAttrs) {
        if (first) first = false; else libfmt::print(fd, ",");
        libfmt::print(fd, "[\"");
        i.nameExpr->showAsJsonArraysFmt(fd);
        libfmt::print(fd, "\",");
        i.valueExpr->showAsJsonArraysFmt(fd);
        libfmt::print(fd, "]");
    }
    libfmt::print(fd, "]]");
}

void ExprList::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprList) ",[");
    bool first = true;
    for (auto & i : elems) {
        if (first) first = false; else libfmt::print(fd, ",");
        i->showAsJsonArraysFmt(fd);
    }
    libfmt::print(fd, "]]");
}

// https://nixos.wiki/wiki/Nix_Expression_Language#Functions
void ExprLambda::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprLambda) ",{}", (matchAttrs ? '1' : '0'));

    if (matchAttrs) {
        libfmt::print(fd, ",[");
        bool first = true;
        for (auto & i : formals->formals) {
            if (first) first = false; else libfmt::print(fd, ",");
            libfmt::print(fd, "[\"");
            String_showAsJsonArraysFmt(fd, i.name);
            libfmt::print(fd, "\"");
            if (i.def) {
                libfmt::print(fd, ",");
                i.def->showAsJsonArraysFmt(fd);
            }
            libfmt::print(fd, "]");
        }
        libfmt::print(fd, "],{}", (formals->ellipsis ? '1' : '0'));
    }
    else {
        libfmt::print(fd, ",0,0");
    }

    if (!arg.empty()) {
        libfmt::print(fd, ",\"{}\",", arg);
    }
    else {
        libfmt::print(fd, ",0,");
    }

    body->showAsJsonArraysFmt(fd);
    libfmt::print(fd, "]");
}

// https://nixos.wiki/wiki/Nix_Expression_Language#let_..._in_statement
void ExprLet::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprLet) ",[");
    bool first = true;
    for (auto & i : attrs->attrs) {
        if (first) first = false; else libfmt::print(fd, ",");
        libfmt::print(fd, "[{},\"", (i.second.inherited ? '1' : '0'));
        String_showAsJsonArraysFmt(fd, i.first);
        libfmt::print(fd, "\"");
        if (!i.second.inherited) {
            libfmt::print(fd, ",");
            i.second.e->showAsJsonArraysFmt(fd);
        }
        libfmt::print(fd, "]");
    }
    libfmt::print(fd, "],");
    body->showAsJsonArraysFmt(fd);
    libfmt::print(fd, "]");
}

// https://nixos.wiki/wiki/Nix_Expression_Language#with_statement
void ExprWith::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprWith) ",");
    attrs->showAsJsonArraysFmt(fd);
    libfmt::print(fd, ",");
    body->showAsJsonArraysFmt(fd);
    libfmt::print(fd, "]");
}

void ExprIf::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprIf) ",");
    cond->showAsJsonArraysFmt(fd);
    libfmt::print(fd, ",");
    then->showAsJsonArraysFmt(fd);
    libfmt::print(fd, ",");
    else_->showAsJsonArraysFmt(fd);
    libfmt::print(fd, "]");
}

void ExprAssert::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprAssert) ",");
    cond->showAsJsonArraysFmt(fd);
    libfmt::print(fd, ",");
    body->showAsJsonArraysFmt(fd);
    libfmt::print(fd, "]");
}

void ExprOpNot::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprOpNot) ",");
    e->showAsJsonArraysFmt(fd);
    libfmt::print(fd, "]");
}

void ExprConcatStrings::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprConcatStrings) ",[");
    bool first = true;
    for (auto & i : *es) {
        if (first) first = false; else libfmt::print(fd, ",");
        i->showAsJsonArraysFmt(fd);
    }
    libfmt::print(fd, "]]");
}

void ExprPos::showAsJsonArraysFmt(FILE *fd) const
{
    libfmt::print(fd, "[" TYPEIDSTR(ExprPos) "]");
}


void AttrPath_showAsJsonArraysFmt(FILE *fd, const AttrPath & attrPath)
{
    libfmt::print(fd, "[");
    bool first = true;
    for (auto & i : attrPath) {
        if (first) first = false; else libfmt::print(fd, ",");
        if (i.symbol.set()) {
            // index 0: isExpr
            libfmt::print(fd, "[0,\"");
            String_showAsJsonArraysFmt(fd, i.symbol);
            libfmt::print(fd, "\"]");
        }
        else {
            libfmt::print(fd, "[1,");
            i.expr->showAsJsonArraysFmt(fd);
            libfmt::print(fd, "]");
        }
    }
    libfmt::print(fd, "]");
}

}

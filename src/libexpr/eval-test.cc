#include "nixexpr.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "nixexpr-ast.hh"

#include <cstdlib>
#include <cstring>

using namespace nix;


void doTest(string s)
{
    EvalState state;
    Expr e = parseExprFromString(state, s, absPath("."));
    printMsg(lvlError, format(">>>>> %1%") % e);
    Value v;
    state.strictEval(e, v);
    printMsg(lvlError, format("result: %1%") % v);
}


void run(Strings args)
{
    printMsg(lvlError, format("size of value: %1% bytes") % sizeof(Value));
    
    doTest("123");
    doTest("{ x = 1; y = 2; }");
    doTest("{ x = 1; y = 2; }.y");
    doTest("rec { x = 1; y = x; }.y");
    doTest("(x: x) 1");
    doTest("(x: y: y) 1 2");
    doTest("x: x");
    doTest("({x, y}: x) { x = 1; y = 2; }");
    doTest("({x, y}@args: args.x) { x = 1; y = 2; }");
    doTest("(args@{x, y}: args.x) { x = 1; y = 2; }");
    doTest("({x ? 1}: x) { }");
    doTest("({x ? 1, y ? x}: y) { x = 2; }");
    doTest("({x, y, ...}: x) { x = 1; y = 2; z = 3; }");
    doTest("({x, y, ...}@args: args.z) { x = 1; y = 2; z = 3; }");
    //doTest("({x ? y, y ? x}: y) { }");
    doTest("let x = 1; in x");
    doTest("with { x = 1; }; x");
    doTest("let x = 2; in with { x = 1; }; x"); // => 2
    doTest("with { x = 1; }; with { x = 2; }; x"); // => 1
    doTest("[ 1 2 3 ]");
    doTest("[ 1 2 ] ++ [ 3 4 5 ]");
    doTest("123 == 123");
    doTest("123 == 456");
    doTest("let id = x: x; in [1 2] == [(id 1) (id 2)]");
    doTest("let id = x: x; in [1 2] == [(id 1) (id 3)]");
    doTest("[1 2] == [3 (let x = x; in x)]");
    doTest("{ x = 1; y.z = 2; } == { y = { z = 2; }; x = 1; }");
    doTest("{ x = 1; y = 2; } == { x = 2; }");
    doTest("{ x = [ 1 2 ]; } == { x = [ 1 ] ++ [ 2 ]; }");
    doTest("1 != 1");
    doTest("true");
    doTest("builtins.true");
    doTest("true == false");
    doTest("__head [ 1 2 3 ]");
    doTest("__add 1 2");
    doTest("null");
    doTest("null");
    doTest("\"foo\"");
    doTest("let s = \"bar\"; in \"foo${s}\"");
    doTest("if true then 1 else 2");
    doTest("if false then 1 else 2");
    doTest("if false || true then 1 else 2");
    doTest("let x = x; in if true || x then 1 else 2");
    doTest("/etc/passwd");
    //doTest("import ./foo.nix");
    doTest("map (x: __add 1 x) [ 1 2 3 ]");
    doTest("map (builtins.add 1) [ 1 2 3 ]");
}


void printHelp()
{
}


string programId = "eval-test";

#include "nixexpr.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"

#include <iostream>
#include <cstdlib>
#include <cstring>

using namespace nix;


void doTest(EvalState & state, string s)
{
    Expr * e = parseExprFromString(state, s, absPath("."));
    std::cerr << ">>>>> " << *e << std::endl;
    Value v;
    state.eval(e, v);
    state.strictForceValue(v);
    printMsg(lvlError, format("result: %1%") % v);
}


void run(Strings args)
{
    SymbolTable t;

    printMsg(lvlError, format("size of symbol: %1% bytes") % sizeof(Symbol));
    
    Symbol s1 = t.create("foo");
    Symbol s2 = t.create("foo");
    Symbol s3 = t.create("bar");
    Symbol s4 = t.create("foo");

    assert(s1 == s2);
    assert(s1 == s4);
    assert(s1 != s3);

    std::map<Symbol, int> m;

    m[s1] = 123;
    m[s3] = 456;

    std::cout << m[s1] << std::endl;
    std::cout << m[s2] << std::endl;
    std::cout << m[s3] << std::endl;
    std::cout << m[s4] << std::endl;

    EvalState state;

    printMsg(lvlError, format("size of value: %1% bytes") % sizeof(Value));
    printMsg(lvlError, format("size of int AST node: %1% bytes") % sizeof(ExprInt));
    printMsg(lvlError, format("size of attrset AST node: %1% bytes") % sizeof(ExprAttrs));

    doTest(state, "123");
    doTest(state, "{ x = 1; y = 2; }");
    doTest(state, "{ x = 1; y = 2; }.y");
    doTest(state, "let x = 1; y = 2; z = 3; in let a = 4; in y");
    doTest(state, "rec { x = 1; y = x; }.x");
    doTest(state, "(x: x) 1");
    doTest(state, "(x: y: y) 1 2");
    doTest(state, "x: x");
    doTest(state, "({x, y}: x) { x = 1; y = 2; }");
    doTest(state, "({x, y}@args: args.x) { x = 1; y = 2; }");
    doTest(state, "(args@{x, y}: args.x) { x = 1; y = 2; }");
    doTest(state, "({x ? 1}: x) { }");
    doTest(state, "({x ? 1, y ? x}: y) { x = 2; }");
    doTest(state, "({x, y, ...}: x) { x = 1; y = 2; z = 3; }");
    doTest(state, "({x, y, ...}@args: args.z) { x = 1; y = 2; z = 3; }");
    //doTest(state, "({x ? y, y ? x}: y) { }");
    doTest(state, "let x = 1; in x");
    doTest(state, "let { x = 1; body = x; }");
    doTest(state, "with { x = 1; }; x");
    doTest(state, "let x = 2; in with { x = 1; }; x"); // => 2
    doTest(state, "with { x = 1; }; with { x = 2; }; x"); // => 2
    doTest(state, "with { x = 1; }; with { y = 2; }; x"); // => 1
    doTest(state, "[ 1 2 3 ]");
    doTest(state, "[ 1 2 ] ++ [ 3 4 5 ]");
    doTest(state, "123 == 123");
    doTest(state, "123 == 456");
    doTest(state, "let id = x: x; in [1 2] == [(id 1) (id 2)]");
    doTest(state, "let id = x: x; in [1 2] == [(id 1) (id 3)]");
    doTest(state, "[1 2] == [3 (let x = x; in x)]");
    doTest(state, "{ x = 1; y.z = 2; } == { y = { z = 2; }; x = 1; }");
    doTest(state, "{ x = 1; y = 2; } == { x = 2; }");
    doTest(state, "{ x = [ 1 2 ]; } == { x = [ 1 ] ++ [ 2 ]; }");
    doTest(state, "1 != 1");
    doTest(state, "true");
    doTest(state, "builtins.true");
    doTest(state, "true == false");
    doTest(state, "__head [ 1 2 3 ]");
    doTest(state, "__add 1 2");
    doTest(state, "null");
    doTest(state, "\"foo\"");
    doTest(state, "''\n  foo\n  bar\n    ''");
    doTest(state, "let s = \"bar\"; in \"foo${s}\"");
    doTest(state, "if true then 1 else 2");
    doTest(state, "if false then 1 else 2");
    doTest(state, "if false || true then 1 else 2");
    doTest(state, "!(true || false)");
    doTest(state, "let x = x; in if true || x then 1 else 2");
    doTest(state, "http://nixos.org/");
    doTest(state, "/etc/passwd");
    //doTest(state, "import ./foo.nix");
    doTest(state, "map (x: __add 1 x) [ 1 2 3 ]");
    doTest(state, "map (builtins.add 1) [ 1 2 3 ]");
    doTest(state, "builtins.hasAttr \"x\" { x = 1; }");
    doTest(state, "let x = 1; as = { inherit x; y = as.x; }; in as.y");
    doTest(state, "let x = 1; as = rec { inherit x; y = x; }; in as.y");
    doTest(state, "let as = { x = 1; }; bs = rec { inherit (as) x; y = x; }; in bs.y");
    doTest(state, "let as = rec { inherit (y) x; y = { x = 1; }; }; in as.x");
    doTest(state, "let x = 1; in let inherit x; in x");
    doTest(state, "with { x = 1; }; let inherit x; y = x; in y");
    doTest(state, "builtins.toXML 123");
    doTest(state, "builtins.toXML { a.b = \"x\" + \"y\"; c = [ 1 2 ] ++ [ 3 4 ]; }");

    state.printStats();
}


void printHelp()
{
}


string programId = "eval-test";

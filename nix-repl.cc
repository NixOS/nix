#include <iostream>
#include <cstdlib>

#include <readline/readline.h>
#include <readline/history.h>

#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "store-api.hh"

using namespace std;
using namespace nix;


string programId = "nix-repl";


struct NixRepl
{
    string curDir;
    EvalState state;

    StaticEnv staticEnv;
    Env * env;
    int displ;

    NixRepl();
    void mainLoop();
    void processLine(string line);
    void addVar(const Symbol & name, Value * v);
    Expr * parseString(string s);
    void evalString(string s, Value & v);
};


void printHelp()
{
    std::cout << "Usage: nix-repl\n";
}


bool getLine(string & line)
{
    char * s = readline("nix-repl> ");
    if (!s) return false;
    line = chomp(string(s));
    free(s);
    if (line != "") add_history(line.c_str());
    return true;
}


NixRepl::NixRepl()
    : staticEnv(false, &state.staticBaseEnv)
{
    curDir = absPath(".");

    env = &state.allocEnv(32768);
    env->up = &state.baseEnv;
    displ = 0;

    store = openStore();
}


void NixRepl::mainLoop()
{
    std::cerr << "Welcome to Nix version " << NIX_VERSION << ". Type :? for help." << std::endl << std::endl;

    while (true) {
        string line;
        if (!getLine(line)) break;

        /* Remove preceeding whitespace. */
        size_t n = line.find_first_not_of(" \n\r\t");
        if (n != string::npos) line = string(line, n);

        try {
            processLine(line);
        } catch (Error & e) {
            printMsg(lvlError, e.msg());
        }

        std::cout << std::endl;
    }

    std::cout << std::endl;
}


void NixRepl::processLine(string line)
{
    if (string(line, 0, 2) == ":a") {
        Value v;
        evalString(string(line, 2), v);
        state.forceAttrs(v);
        foreach (Bindings::iterator, i, *v.attrs)
            addVar(i->name, i->value);
    }

    else if (string(line, 0, 2) == ":t") {
        Value v;
        evalString(string(line, 2), v);
        std::cout << showType(v) << std::endl;
    }

    else if (string(line, 0, 1) == ":") {
        throw Error(format("unknown command ‘%1%’") % string(line, 0, 2));
    }

    else {
        Value v;
        evalString(line, v);
        state.strictForceValue(v);
        std::cout << v << std::endl;
    }
}


void NixRepl::addVar(const Symbol & name, Value * v)
{
    staticEnv.vars[name] = displ;
    env->values[displ++] = v;
}


Expr * NixRepl::parseString(string s)
{
    Expr * e = state.parseExprFromString(s, curDir, staticEnv);
    return e;
}


void NixRepl::evalString(string s, Value & v)
{
    Expr * e = parseString(s);
    e->eval(state, *env, v);
    state.forceValue(v);
}


void run(nix::Strings args)
{
    NixRepl repl;
    repl.mainLoop();
}

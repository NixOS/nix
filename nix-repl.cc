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
        Expr * e = parseString(string(line, 2));
        Value v;
        e->eval(state, *env, v);
        state.forceAttrs(v);
        foreach (Bindings::iterator, i, *v.attrs)
            addVar(i->name, i->value);
    }

    else if (string(line, 0, 1) == ":") {
        throw Error(format("unknown command ‘%1%’") % string(line, 0, 2));
    }

    else {
        Expr * e = parseString(line);
        Value v;
        e->eval(state, *env, v);
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


void run(nix::Strings args)
{
    NixRepl repl;
    repl.mainLoop();
}

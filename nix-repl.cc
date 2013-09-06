#include <iostream>
#include <cstdlib>

#include <setjmp.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "common-opts.hh"

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
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol & name, Value * v);
    Expr * parseString(string s);
    void evalString(string s, Value & v);
};


void printHelp()
{
    std::cout << "Usage: nix-repl\n";
}


/* Apparently, the only way to get readline() to return on Ctrl-C
   (SIGINT) is to use siglongjmp().  That's fucked up... */
static sigjmp_buf sigintJmpBuf;


static void sigintHandler(int signo)
{
    siglongjmp(sigintJmpBuf, 1);
}


bool getLine(string & line)
{
    struct sigaction act, old;
    act.sa_handler = sigintHandler;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, &old))
        throw SysError("installing handler for SIGINT");

    if (sigsetjmp(sigintJmpBuf, 1))
        line = "";
    else {
        char * s = readline("nix-repl> ");
        if (!s) return false;
        line = chomp(string(s));
        free(s);
        if (line != "") {
            add_history(line.c_str());
            append_history(1, 0);
        }
    }

    _isInterrupted = 0;

    if (sigaction(SIGINT, &old, 0))
        throw SysError("restoring handler for SIGINT");

    return true;
}


string removeWhitespace(string s)
{
    s = chomp(s);
    size_t n = s.find_first_not_of(" \n\r\t");
    if (n != string::npos) s = string(s, n);
    return s;
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

    using_history();
    read_history(0);

    while (true) {
        string line;
        if (!getLine(line)) break;

        try {
            processLine(removeWhitespace(line));
        } catch (Error & e) {
            printMsg(lvlError, e.msg());
        }

        std::cout << std::endl;
    }

    std::cout << std::endl;
}


void NixRepl::processLine(string line)
{
    if (line == "") return;

    if (string(line, 0, 2) == ":a") {
        Value v;
        evalString(string(line, 2), v);
        addAttrsToScope(v);
    }

    else if (string(line, 0, 2) == ":l") {
        state.resetFileCache();
        Path path = lookupFileArg(state, removeWhitespace(string(line, 2)));
        Value v, v2;
        state.evalFile(path, v);
        Bindings bindings;
        state.autoCallFunction(bindings, v, v2);
        addAttrsToScope(v2);
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


void NixRepl::addAttrsToScope(Value & attrs)
{
    state.forceAttrs(attrs);
    foreach (Bindings::iterator, i, *attrs.attrs)
        addVarToScope(i->name, i->value);
}


void NixRepl::addVarToScope(const Symbol & name, Value * v)
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

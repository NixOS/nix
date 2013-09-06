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
#include "get-drvs.hh"
#include "derivations.hh"

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
            printMsg(lvlError, "error: " + e.msg());
        } catch (Interrupted & e) {
            printMsg(lvlError, "error: " + e.msg());
        }

        std::cout << std::endl;
    }

    std::cout << std::endl;
}


void NixRepl::processLine(string line)
{
    if (line == "") return;

    string command = string(line, 0, 2);

    if (command == ":a") {
        Value v;
        evalString(string(line, 2), v);
        addAttrsToScope(v);
    }

    else if (command == ":l") {
        state.resetFileCache();
        Path path = lookupFileArg(state, removeWhitespace(string(line, 2)));
        Value v, v2;
        state.evalFile(path, v);
        Bindings bindings;
        state.autoCallFunction(bindings, v, v2);
        addAttrsToScope(v2);
    }

    else if (command == ":t") {
        Value v;
        evalString(string(line, 2), v);
        std::cout << showType(v) << std::endl;
    }

    else if (command == ":b" || command == ":s") {
        Value v;
        evalString(string(line, 2), v);
        DrvInfo drvInfo;
        if (!getDerivation(state, v, drvInfo, false))
            throw Error("expression does not evaluation to a derivation, so I can't build it");
        Path drvPath = drvInfo.queryDrvPath(state);
        if (drvPath == "" || !store->isValidPath(drvPath))
            throw Error("expression did not evaluate to a valid derivation");

        if (command == ":b") {
            /* We could do the build in this process using buildPaths(),
               but doing it in a child makes it easier to recover from
               problems / SIGINT. */
            if (system(("nix-store -r " + drvPath + " > /dev/null").c_str()) == -1)
                throw SysError("starting nix-store");
            Derivation drv = parseDerivation(readFile(drvPath));
            std::cout << "this derivation produced the following outputs:" << std::endl;
            foreach (DerivationOutputs::iterator, i, drv.outputs)
                std::cout << format("  %1% -> %2%") % i->first % i->second.path << std::endl;
        } else {
            if (system(("nix-shell " + drvPath).c_str()) == -1)
                throw SysError("starting nix-shell");
        }
    }

    else if (string(line, 0, 1) == ":")
        throw Error(format("unknown command ‘%1%’") % string(line, 0, 2));

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
    std::cout << format("added %1% variables") % attrs.attrs->size() << std::endl;
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

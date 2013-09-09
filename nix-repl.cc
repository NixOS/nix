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
#include "affinity.hh"

using namespace std;
using namespace nix;


string programId = "nix-repl";


struct NixRepl
{
    string curDir;
    EvalState state;

    Strings loadedFiles;

    StaticEnv staticEnv;
    Env * env;
    int displ;
    StringSet varNames;

    StringSet completions;
    StringSet::iterator curCompletion;

    NixRepl();
    void mainLoop(const Strings & args);
    void completePrefix(string prefix);
    bool getLine(string & line);
    bool processLine(string line);
    void loadFile(const Path & path);
    void reloadFiles();
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol & name, Value & v);
    Expr * parseString(string s);
    void evalString(string s, Value & v);

    typedef set<Value *> ValuesSeen;
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth);
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth, ValuesSeen & seen);
};


void printHelp()
{
    std::cout << "Usage: nix-repl\n";
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


void NixRepl::mainLoop(const Strings & args)
{
    std::cout << "Welcome to Nix version " << NIX_VERSION << ". Type :? for help." << std::endl << std::endl;

    foreach (Strings::const_iterator, i, args)
        loadedFiles.push_back(*i);

    if (!loadedFiles.empty()) {
        reloadFiles();
        std::cout << std::endl;
    }

    using_history();
    read_history(0);

    while (true) {
        string line;
        if (!getLine(line)) {
            std::cout << std::endl;
            break;
        }

        try {
            if (!processLine(removeWhitespace(line))) return;
        } catch (Error & e) {
            printMsg(lvlError, "error: " + e.msg());
        } catch (Interrupted & e) {
            printMsg(lvlError, "error: " + e.msg());
        }

        std::cout << std::endl;
    }
}


/* Apparently, the only way to get readline() to return on Ctrl-C
   (SIGINT) is to use siglongjmp().  That's fucked up... */
static sigjmp_buf sigintJmpBuf;


static void sigintHandler(int signo)
{
    siglongjmp(sigintJmpBuf, 1);
}


/* Oh, if only g++ had nested functions... */
NixRepl * curRepl;

char * completerThunk(const char * s, int state)
{
    string prefix(s);

    /* If the prefix has a slash in it, use readline's builtin filename
       completer. */
    if (prefix.find('/') != string::npos)
        return rl_filename_completion_function(s, state);

    /* Otherwise, return all symbols that start with the prefix. */
    if (state == 0) {
        curRepl->completePrefix(s);
        curRepl->curCompletion = curRepl->completions.begin();
    }
    if (curRepl->curCompletion == curRepl->completions.end()) return 0;
    return strdup((curRepl->curCompletion++)->c_str());
}


bool NixRepl::getLine(string & line)
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
        curRepl = this;
        rl_completion_entry_function = completerThunk;

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


void NixRepl::completePrefix(string prefix)
{
    completions.clear();

    size_t dot = prefix.rfind('.');

    if (dot == string::npos) {
        /* This is a variable name; look it up in the current scope. */
        StringSet::iterator i = varNames.lower_bound(prefix);
        while (i != varNames.end()) {
            if (string(*i, 0, prefix.size()) != prefix) break;
            completions.insert(*i);
            i++;
        }
    } else {
        try {
            /* This is an expression that should evaluate to an
               attribute set.  Evaluate it to get the names of the
               attributes. */
            string expr(prefix, 0, dot);
            string prefix2 = string(prefix, dot + 1);

            Expr * e = parseString(expr);
            Value v;
            e->eval(state, *env, v);
            state.forceAttrs(v);

            foreach (Bindings::iterator, i, *v.attrs) {
                string name = i->name;
                if (string(name, 0, prefix2.size()) != prefix2) continue;
                completions.insert(expr + "." + name);
            }

        } catch (ParseError & e) {
            // Quietly ignore parse errors.
        }catch (EvalError & e) {
            // Quietly ignore evaluation errors.
        }
    }
}


static int runProgram(const string & program, const Strings & args)
{
    std::vector<const char *> cargs; /* careful with c_str()! */
    cargs.push_back(program.c_str());
    for (Strings::const_iterator i = args.begin(); i != args.end(); ++i)
        cargs.push_back(i->c_str());
    cargs.push_back(0);

    Pid pid;
    pid = fork();
    if (pid == -1) throw SysError("forking");
    if (pid == 0) {
        restoreAffinity();
        execvp(program.c_str(), (char * *) &cargs[0]);
        _exit(1);
    }

    return pid.wait(true);
}


bool isVarName(const string & s)
{
    // FIXME: not quite correct.
    foreach (string::const_iterator, i, s)
        if (!((*i >= 'a' && *i <= 'z') ||
              (*i >= 'A' && *i <= 'Z') ||
              (*i >= '0' && *i <= '9') ||
              *i == '_' || *i == '\''))
            return false;
    return true;
}


bool NixRepl::processLine(string line)
{
    if (line == "") return true;

    string command, arg;

    if (line[0] == ':') {
        size_t p = line.find(' ');
        command = string(line, 0, p);
        if (p != string::npos) arg = removeWhitespace(string(line, p));
    } else {
        arg = line;
    }

    if (command == ":?" || command == ":help") {
        cout << "The following commands are available:\n"
             << "\n"
             << "  <expr>        Evaluate and print expression\n"
             << "  <x> = <expr>  Bind expression to variable\n"
             << "  :a <expr>     Add attributes from resulting set to scope\n"
             << "  :b <expr>     Build derivation\n"
             << "  :l <path>     Load Nix expression and add it to scope\n"
             << "  :p <expr>     Evaluate and print expression recursively\n"
             << "  :q            Exit nix-repl\n"
             << "  :r            Reload all files\n"
             << "  :s <expr>     Build dependencies of derivation, then start nix-shell\n"
             << "  :t <expr>     Describe result of evaluation\n";
    }

    else if (command == ":a" || command == ":add") {
        Value v;
        evalString(arg, v);
        addAttrsToScope(v);
    }

    else if (command == ":l" || command == ":load") {
        state.resetFileCache();
        loadFile(arg);
    }

    else if (command == ":r" || command == ":reload") {
        state.resetFileCache();
        reloadFiles();
    }

    else if (command == ":t") {
        Value v;
        evalString(arg, v);
        std::cout << showType(v) << std::endl;
    }

    else if (command == ":b" || command == ":s") {
        Value v;
        evalString(arg, v);
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
            if (runProgram("nix-store", Strings{"-r", drvPath}) == 0) {
                Derivation drv = parseDerivation(readFile(drvPath));
                std::cout << std::endl << "this derivation produced the following outputs:" << std::endl;
                foreach (DerivationOutputs::iterator, i, drv.outputs)
                    std::cout << format("  %1% -> %2%") % i->first % i->second.path << std::endl;
            }
        } else
            runProgram("nix-shell", Strings{drvPath});
    }

    else if (command == ":p" || command == ":print") {
        Value v;
        evalString(arg, v);
        printValue(std::cout, v, 1000000000) << std::endl;
    }

    else if (command == ":q" || command == ":quit")
        return false;

    else if (command != "")
        throw Error(format("unknown command ‘%1%’") % command);

    else {
        size_t p = line.find('=');
        string name;
        if (p != string::npos &&
            isVarName(name = removeWhitespace(string(line, 0, p))))
        {
            Expr * e = parseString(string(line, p + 1));
            Value & v(*state.allocValue());
            v.type = tThunk;
            v.thunk.env = env;
            v.thunk.expr = e;
            addVarToScope(state.symbols.create(name), v);
        } else {
            Value v;
            evalString(line, v);
            printValue(std::cout, v, 1) << std::endl;
        }
    }

    return true;
}


void NixRepl::loadFile(const Path & path)
{
    loadedFiles.remove(path);
    loadedFiles.push_back(path);
    Value v, v2;
    state.evalFile(lookupFileArg(state, path), v);
    Bindings bindings;
    state.autoCallFunction(bindings, v, v2);
    addAttrsToScope(v2);
}


void NixRepl::reloadFiles()
{
    Strings old = loadedFiles;
    loadedFiles.clear();

    foreach (Strings::iterator, i, old) {
        if (i != old.begin()) std::cout << std::endl;
        std::cout << format("Loading ‘%1%’...") % *i << std::endl;
        loadFile(*i);
    }
}


void NixRepl::addAttrsToScope(Value & attrs)
{
    state.forceAttrs(attrs);
    foreach (Bindings::iterator, i, *attrs.attrs)
        addVarToScope(i->name, *i->value);
    std::cout << format("Added %1% variables.") % attrs.attrs->size() << std::endl;
}


void NixRepl::addVarToScope(const Symbol & name, Value & v)
{
    staticEnv.vars[name] = displ;
    env->values[displ++] = &v;
    varNames.insert((string) name);
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


std::ostream & NixRepl::printValue(std::ostream & str, Value & v, unsigned int maxDepth)
{
    ValuesSeen seen;
    return printValue(str, v, maxDepth, seen);
}


// FIXME: lot of cut&paste from Nix's eval.cc.
std::ostream & NixRepl::printValue(std::ostream & str, Value & v, unsigned int maxDepth, ValuesSeen & seen)
{
    str.flush();
    checkInterrupt();

    state.forceValue(v);

    switch (v.type) {

    case tInt:
        str << v.integer;
        break;

    case tBool:
        str << (v.boolean ? "true" : "false");
        break;

    case tString:
        str << "\"";
        for (const char * i = v.string.s; *i; i++)
            if (*i == '\"' || *i == '\\') str << "\\" << *i;
            else if (*i == '\n') str << "\\n";
            else if (*i == '\r') str << "\\r";
            else if (*i == '\t') str << "\\t";
            else str << *i;
        str << "\"";
        break;

    case tPath:
        str << v.path; // !!! escaping?
        break;

    case tNull:
        str << "null";
        break;

    case tAttrs: {
        seen.insert(&v);

        bool isDrv = state.isDerivation(v);
        if (isDrv) str << "(derivation ";
        str << "{ ";

        if (maxDepth > 0) {
            typedef std::map<string, Value *> Sorted;
            Sorted sorted;
            foreach (Bindings::iterator, i, *v.attrs)
                sorted[i->name] = i->value;

            /* If this is a derivation, then don't show the
               self-references ("all", "out", etc.). */
            StringSet hidden;
            if (isDrv) {
                hidden.insert("all");
                Bindings::iterator i = v.attrs->find(state.sOutputs);
                if (i == v.attrs->end())
                    hidden.insert("out");
                else {
                    state.forceList(*i->value);
                    for (unsigned int j = 0; j < i->value->list.length; ++j)
                        hidden.insert(state.forceStringNoCtx(*i->value->list.elems[j]));
                }
            }

            foreach (Sorted::iterator, i, sorted) {
                str << i->first << " = ";
                if (hidden.find(i->first) != hidden.end())
                    str << "«...»";
                else if (seen.find(i->second) != seen.end())
                    str << "«repeated»";
                else
                    try {
                        printValue(str, *i->second, maxDepth - 1, seen);
                    } catch (AssertionError & e) {
                        str << "«error: " << e.msg() << "»";
                    }
                str << "; ";
            }

        } else
            str << "... ";

        str << "}";
        if (isDrv) str << ")";
        break;
    }

    case tList:
        seen.insert(&v);

        str << "[ ";
        if (maxDepth > 0)
            for (unsigned int n = 0; n < v.list.length; ++n) {
                if (seen.find(v.list.elems[n]) != seen.end())
                    str << "«repeated»";
                else
                    try {
                        printValue(str, *v.list.elems[n], maxDepth - 1, seen);
                    } catch (AssertionError & e) {
                        str << "«error: " << e.msg() << "»";
                    }
                str << " ";
            }
        else
            str << "... ";
        str << "]";
        break;

    case tLambda:
        str << "«lambda»";
        break;

    case tPrimOp:
        str << "«primop»";
        break;

    case tPrimOpApp:
        str << "«primop-app»";
        break;

    default:
        str << "«unknown»";
        break;
    }

    return str;
}


void run(Strings args)
{
    NixRepl repl;
    repl.mainLoop(args);
}

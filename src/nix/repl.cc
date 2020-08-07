#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>

#include <setjmp.h>

#ifdef READLINE
#include <readline/history.h>
#include <readline/readline.h>
#else
// editline < 1.15.2 don't wrap their API for C++ usage
// (added in https://github.com/troglobit/editline/commit/91398ceb3427b730995357e9d120539fb9bb7461).
// This results in linker errors due to to name-mangling of editline C symbols.
// For compatibility with these versions, we wrap the API here
// (wrapping multiple times on newer versions is no problem).
extern "C" {
#include <editline.h>
}
#endif

#include "ansicolor.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "attr-path.hh"
#include "store-api.hh"
#include "common-eval-args.hh"
#include "get-drvs.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "globals.hh"
#include "command.hh"
#include "finally.hh"

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc_cpp.h>
#endif

namespace nix {

struct NixRepl
    #if HAVE_BOEHMGC
    : gc
    #endif
{
    string curDir;
    std::unique_ptr<EvalState> state;
    Bindings * autoArgs;

    Strings loadedFiles;

    const static int envSize = 32768;
    StaticEnv staticEnv;
    Env * env;
    int displ;
    StringSet varNames;

    const Path historyFile;

    NixRepl(const Strings & searchPath, nix::ref<Store> store);
    ~NixRepl();
    void mainLoop(const std::vector<std::string> & files);
    StringSet completePrefix(string prefix);
    bool getLine(string & input, const std::string &prompt);
    Path getDerivationPath(Value & v);
    bool processLine(string line);
    void loadFile(const Path & path);
    void initEnv();
    void reloadFiles();
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol & name, Value & v);
    Expr * parseString(string s);
    void evalString(string s, Value & v);

    typedef set<Value *> ValuesSeen;
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth);
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth, ValuesSeen & seen);
};


string removeWhitespace(string s)
{
    s = chomp(s);
    size_t n = s.find_first_not_of(" \n\r\t");
    if (n != string::npos) s = string(s, n);
    return s;
}


NixRepl::NixRepl(const Strings & searchPath, nix::ref<Store> store)
    : state(std::make_unique<EvalState>(searchPath, store))
    , staticEnv(false, &state->staticBaseEnv)
    , historyFile(getDataDir() + "/nix/repl-history")
{
    curDir = absPath(".");
}


NixRepl::~NixRepl()
{
    write_history(historyFile.c_str());
}

static NixRepl * curRepl; // ugly

static char * completionCallback(char * s, int *match) {
  auto possible = curRepl->completePrefix(s);
  if (possible.size() == 1) {
    *match = 1;
    auto *res = strdup(possible.begin()->c_str() + strlen(s));
    if (!res) throw Error("allocation failure");
    return res;
  } else if (possible.size() > 1) {
    auto checkAllHaveSameAt = [&](size_t pos) {
      auto &first = *possible.begin();
      for (auto &p : possible) {
        if (p.size() <= pos || p[pos] != first[pos])
          return false;
      }
      return true;
    };
    size_t start = strlen(s);
    size_t len = 0;
    while (checkAllHaveSameAt(start + len)) ++len;
    if (len > 0) {
      *match = 1;
      auto *res = strdup(std::string(*possible.begin(), start, len).c_str());
      if (!res) throw Error("allocation failure");
      return res;
    }
  }

  *match = 0;
  return nullptr;
}

static int listPossibleCallback(char *s, char ***avp) {
  auto possible = curRepl->completePrefix(s);

  if (possible.size() > (INT_MAX / sizeof(char*)))
    throw Error("too many completions");

  int ac = 0;
  char **vp = nullptr;

  auto check = [&](auto *p) {
    if (!p) {
      if (vp) {
        while (--ac >= 0)
          free(vp[ac]);
        free(vp);
      }
      throw Error("allocation failure");
    }
    return p;
  };

  vp = check((char **)malloc(possible.size() * sizeof(char*)));

  for (auto & p : possible)
    vp[ac++] = check(strdup(p.c_str()));

  *avp = vp;

  return ac;
}

namespace {
    // Used to communicate to NixRepl::getLine whether a signal occurred in ::readline.
    volatile sig_atomic_t g_signal_received = 0;

    void sigintHandler(int signo) {
        g_signal_received = signo;
    }
}

void NixRepl::mainLoop(const std::vector<std::string> & files)
{
    string error = ANSI_RED "error:" ANSI_NORMAL " ";
    std::cout << "Welcome to Nix version " << nixVersion << ". Type :? for help." << std::endl << std::endl;

    for (auto & i : files)
        loadedFiles.push_back(i);

    reloadFiles();
    if (!loadedFiles.empty()) std::cout << std::endl;

    // Allow nix-repl specific settings in .inputrc
    rl_readline_name = "nix-repl";
    createDirs(dirOf(historyFile));
#ifndef READLINE
    el_hist_size = 1000;
#endif
    read_history(historyFile.c_str());
    curRepl = this;
#ifndef READLINE
    rl_set_complete_func(completionCallback);
    rl_set_list_possib_func(listPossibleCallback);
#endif

    std::string input;

    while (true) {
        // When continuing input from previous lines, don't print a prompt, just align to the same
        // number of chars as the prompt.
        if (!getLine(input, input.empty() ? "nix-repl> " : "          "))
            break;

        try {
            if (!removeWhitespace(input).empty() && !processLine(input)) return;
        } catch (ParseError & e) {
            if (e.msg().find("unexpected $end") != std::string::npos) {
                // For parse errors on incomplete input, we continue waiting for the next line of
                // input without clearing the input so far.
                continue;
            } else {
              printMsg(lvlError, e.msg());
            }
        } catch (Error & e) {
          printMsg(lvlError, e.msg());
        } catch (Interrupted & e) {
          printMsg(lvlError, e.msg());
        }

        // We handled the current input fully, so we should clear it
        // and read brand new input.
        input.clear();
        std::cout << std::endl;
    }
}


bool NixRepl::getLine(string & input, const std::string &prompt)
{
    struct sigaction act, old;
    sigset_t savedSignalMask, set;

    auto setupSignals = [&]() {
        act.sa_handler = sigintHandler;
        sigfillset(&act.sa_mask);
        act.sa_flags = 0;
        if (sigaction(SIGINT, &act, &old))
            throw SysError("installing handler for SIGINT");

        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        if (sigprocmask(SIG_UNBLOCK, &set, &savedSignalMask))
            throw SysError("unblocking SIGINT");
    };
    auto restoreSignals = [&]() {
        if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
            throw SysError("restoring signals");

        if (sigaction(SIGINT, &old, 0))
            throw SysError("restoring handler for SIGINT");
    };

    setupSignals();
    char * s = readline(prompt.c_str());
    Finally doFree([&]() { free(s); });
    restoreSignals();

    if (g_signal_received) {
        g_signal_received = 0;
        input.clear();
        return true;
    }

    if (!s)
      return false;
    input += s;
    input += '\n';
    return true;
}


StringSet NixRepl::completePrefix(string prefix)
{
    StringSet completions;

    size_t start = prefix.find_last_of(" \n\r\t(){}[]");
    std::string prev, cur;
    if (start == std::string::npos) {
        prev = "";
        cur = prefix;
    } else {
        prev = std::string(prefix, 0, start + 1);
        cur = std::string(prefix, start + 1);
    }

    size_t slash, dot;

    if ((slash = cur.rfind('/')) != string::npos) {
        try {
            auto dir = std::string(cur, 0, slash);
            auto prefix2 = std::string(cur, slash + 1);
            for (auto & entry : readDirectory(dir == "" ? "/" : dir)) {
                if (entry.name[0] != '.' && hasPrefix(entry.name, prefix2))
                    completions.insert(prev + dir + "/" + entry.name);
            }
        } catch (Error &) {
        }
    } else if ((dot = cur.rfind('.')) == string::npos) {
        /* This is a variable name; look it up in the current scope. */
        StringSet::iterator i = varNames.lower_bound(cur);
        while (i != varNames.end()) {
            if (string(*i, 0, cur.size()) != cur) break;
            completions.insert(prev + *i);
            i++;
        }
    } else {
        try {
            /* This is an expression that should evaluate to an
               attribute set.  Evaluate it to get the names of the
               attributes. */
            string expr(cur, 0, dot);
            string cur2 = string(cur, dot + 1);

            Expr * e = parseString(expr);
            Value v;
            e->eval(*state, *env, v);
            state->forceAttrs(v);

            for (auto & i : *v.attrs) {
                string name = i.name;
                if (string(name, 0, cur2.size()) != cur2) continue;
                completions.insert(prev + expr + "." + name);
            }

        } catch (ParseError & e) {
            // Quietly ignore parse errors.
        } catch (EvalError & e) {
            // Quietly ignore evaluation errors.
        } catch (UndefinedVarError & e) {
            // Quietly ignore undefined variable errors.
        }
    }

    return completions;
}


static int runProgram(const string & program, const Strings & args)
{
    Strings args2(args);
    args2.push_front(program);

    Pid pid;
    pid = fork();
    if (pid == -1) throw SysError("forking");
    if (pid == 0) {
        restoreAffinity();
        execvp(program.c_str(), stringsToCharPtrs(args2).data());
        _exit(1);
    }

    return pid.wait();
}


bool isVarName(const string & s)
{
    if (s.size() == 0) return false;
    char c = s[0];
    if ((c >= '0' && c <= '9') || c == '-' || c == '\'') return false;
    for (auto & i : s)
        if (!((i >= 'a' && i <= 'z') ||
              (i >= 'A' && i <= 'Z') ||
              (i >= '0' && i <= '9') ||
              i == '_' || i == '-' || i == '\''))
            return false;
    return true;
}


Path NixRepl::getDerivationPath(Value & v) {
    auto drvInfo = getDerivation(*state, v, false);
    if (!drvInfo)
        throw Error("expression does not evaluate to a derivation, so I can't build it");
    Path drvPath = drvInfo->queryDrvPath();
    if (drvPath == "" || !state->store->isValidPath(state->store->parseStorePath(drvPath)))
        throw Error("expression did not evaluate to a valid derivation");
    return drvPath;
}


bool NixRepl::processLine(string line)
{
    if (line == "") return true;

    string command, arg;

    if (line[0] == ':') {
        size_t p = line.find_first_of(" \n\r\t");
        command = string(line, 0, p);
        if (p != string::npos) arg = removeWhitespace(string(line, p));
    } else {
        arg = line;
    }

    if (command == ":?" || command == ":help") {
        std::cout
             << "The following commands are available:\n"
             << "\n"
             << "  <expr>        Evaluate and print expression\n"
             << "  <x> = <expr>  Bind expression to variable\n"
             << "  :a <expr>     Add attributes from resulting set to scope\n"
             << "  :b <expr>     Build derivation\n"
             << "  :e <expr>     Open the derivation in $EDITOR\n"
             << "  :i <expr>     Build derivation, then install result into current profile\n"
             << "  :l <path>     Load Nix expression and add it to scope\n"
             << "  :p <expr>     Evaluate and print expression recursively\n"
             << "  :q            Exit nix-repl\n"
             << "  :r            Reload all files\n"
             << "  :s <expr>     Build dependencies of derivation, then start nix-shell\n"
             << "  :t <expr>     Describe result of evaluation\n"
             << "  :u <expr>     Build derivation, then start nix-shell\n";
    }

    else if (command == ":a" || command == ":add") {
        Value v;
        evalString(arg, v);
        addAttrsToScope(v);
    }

    else if (command == ":l" || command == ":load") {
        state->resetFileCache();
        loadFile(arg);
    }

    else if (command == ":r" || command == ":reload") {
        state->resetFileCache();
        reloadFiles();
    }

    else if (command == ":e" || command == ":edit") {
        Value v;
        evalString(arg, v);

        Pos pos;

        if (v.type == tPath || v.type == tString) {
            PathSet context;
            auto filename = state->coerceToString(noPos, v, context);
            pos.file = state->symbols.create(filename);
        } else if (v.type == tLambda) {
            pos = v.lambda.fun->pos;
        } else {
            // assume it's a derivation
            pos = findDerivationFilename(*state, v, arg);
        }

        // Open in EDITOR
        auto args = editorFor(pos);
        auto editor = args.front();
        args.pop_front();
        runProgram(editor, args);

        // Reload right after exiting the editor
        state->resetFileCache();
        reloadFiles();
    }

    else if (command == ":t") {
        Value v;
        evalString(arg, v);
        std::cout << showType(v) << std::endl;

    } else if (command == ":u") {
        Value v, f, result;
        evalString(arg, v);
        evalString("drv: (import <nixpkgs> {}).runCommand \"shell\" { buildInputs = [ drv ]; } \"\"", f);
        state->callFunction(f, v, result, Pos());

        Path drvPath = getDerivationPath(result);
        runProgram(settings.nixBinDir + "/nix-shell", Strings{drvPath});
    }

    else if (command == ":b" || command == ":i" || command == ":s") {
        Value v;
        evalString(arg, v);
        Path drvPath = getDerivationPath(v);

        if (command == ":b") {
            /* We could do the build in this process using buildPaths(),
               but doing it in a child makes it easier to recover from
               problems / SIGINT. */
            if (runProgram(settings.nixBinDir + "/nix", Strings{"build", "--no-link", drvPath}) == 0) {
                auto outputs = state->store->queryDerivationOutputMapAssumeTotal(state->store->parseStorePath(drvPath));
                std::cout << std::endl << "this derivation produced the following outputs:" << std::endl;
                for (auto & i : outputs)
                    std::cout << fmt("  %s -> %s\n", i.first, state->store->printStorePath(i.second));
            }
        } else if (command == ":i") {
            runProgram(settings.nixBinDir + "/nix-env", Strings{"-i", drvPath});
        } else {
            runProgram(settings.nixBinDir + "/nix-shell", Strings{drvPath});
        }
    }

    else if (command == ":p" || command == ":print") {
        Value v;
        evalString(arg, v);
        printValue(std::cout, v, 1000000000) << std::endl;
    }

    else if (command == ":q" || command == ":quit")
        return false;

    else if (command != "")
        throw Error("unknown command '%1%'", command);

    else {
        size_t p = line.find('=');
        string name;
        if (p != string::npos &&
            p < line.size() &&
            line[p + 1] != '=' &&
            isVarName(name = removeWhitespace(string(line, 0, p))))
        {
            Expr * e = parseString(string(line, p + 1));
            Value & v(*state->allocValue());
            v.type = tThunk;
            v.thunk.env = env;
            v.thunk.expr = e;
            addVarToScope(state->symbols.create(name), v);
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
    state->evalFile(lookupFileArg(*state, path), v);
    state->autoCallFunction(*autoArgs, v, v2);
    addAttrsToScope(v2);
}


void NixRepl::initEnv()
{
    env = &state->allocEnv(envSize);
    env->up = &state->baseEnv;
    displ = 0;
    staticEnv.vars.clear();

    varNames.clear();
    for (auto & i : state->staticBaseEnv.vars)
        varNames.insert(i.first);
}


void NixRepl::reloadFiles()
{
    initEnv();

    Strings old = loadedFiles;
    loadedFiles.clear();

    bool first = true;
    for (auto & i : old) {
        if (!first) std::cout << std::endl;
        first = false;
        std::cout << format("Loading '%1%'...") % i << std::endl;
        loadFile(i);
    }
}


void NixRepl::addAttrsToScope(Value & attrs)
{
    state->forceAttrs(attrs);
    for (auto & i : *attrs.attrs)
        addVarToScope(i.name, *i.value);
    std::cout << format("Added %1% variables.") % attrs.attrs->size() << std::endl;
}


void NixRepl::addVarToScope(const Symbol & name, Value & v)
{
    if (displ >= envSize)
        throw Error("environment full; cannot add more variables");
    staticEnv.vars[name] = displ;
    env->values[displ++] = &v;
    varNames.insert((string) name);
}


Expr * NixRepl::parseString(string s)
{
    Expr * e = state->parseExprFromString(s, curDir, staticEnv);
    return e;
}


void NixRepl::evalString(string s, Value & v)
{
    Expr * e = parseString(s);
    e->eval(*state, *env, v);
    state->forceValue(v);
}


std::ostream & NixRepl::printValue(std::ostream & str, Value & v, unsigned int maxDepth)
{
    ValuesSeen seen;
    return printValue(str, v, maxDepth, seen);
}


std::ostream & printStringValue(std::ostream & str, const char * string) {
    str << "\"";
    for (const char * i = string; *i; i++)
        if (*i == '\"' || *i == '\\') str << "\\" << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
        else str << *i;
    str << "\"";
    return str;
}


// FIXME: lot of cut&paste from Nix's eval.cc.
std::ostream & NixRepl::printValue(std::ostream & str, Value & v, unsigned int maxDepth, ValuesSeen & seen)
{
    str.flush();
    checkInterrupt();

    state->forceValue(v);

    switch (v.type) {

    case tInt:
        str << ANSI_CYAN << v.integer << ANSI_NORMAL;
        break;

    case tBool:
        str << ANSI_CYAN << (v.boolean ? "true" : "false") << ANSI_NORMAL;
        break;

    case tString:
        str << ANSI_YELLOW;
        printStringValue(str, v.string.s);
        str << ANSI_NORMAL;
        break;

    case tPath:
        str << ANSI_GREEN << v.path << ANSI_NORMAL; // !!! escaping?
        break;

    case tNull:
        str << ANSI_CYAN "null" ANSI_NORMAL;
        break;

    case tAttrs: {
        seen.insert(&v);

        bool isDrv = state->isDerivation(v);

        if (isDrv) {
            str << "«derivation ";
            Bindings::iterator i = v.attrs->find(state->sDrvPath);
            PathSet context;
            Path drvPath = i != v.attrs->end() ? state->coerceToPath(*i->pos, *i->value, context) : "???";
            str << drvPath << "»";
        }

        else if (maxDepth > 0) {
            str << "{ ";

            typedef std::map<string, Value *> Sorted;
            Sorted sorted;
            for (auto & i : *v.attrs)
                sorted[i.name] = i.value;

            for (auto & i : sorted) {
                if (isVarName(i.first))
                    str << i.first;
                else
                    printStringValue(str, i.first.c_str());
                str << " = ";
                if (seen.find(i.second) != seen.end())
                    str << "«repeated»";
                else
                    try {
                        printValue(str, *i.second, maxDepth - 1, seen);
                    } catch (AssertionError & e) {
                        str << ANSI_RED "«error: " << e.msg() << "»" ANSI_NORMAL;
                    }
                str << "; ";
            }

            str << "}";
        } else
            str << "{ ... }";

        break;
    }

    case tList1:
    case tList2:
    case tListN:
        seen.insert(&v);

        str << "[ ";
        if (maxDepth > 0)
            for (unsigned int n = 0; n < v.listSize(); ++n) {
                if (seen.find(v.listElems()[n]) != seen.end())
                    str << "«repeated»";
                else
                    try {
                        printValue(str, *v.listElems()[n], maxDepth - 1, seen);
                    } catch (AssertionError & e) {
                        str << ANSI_RED "«error: " << e.msg() << "»" ANSI_NORMAL;
                    }
                str << " ";
            }
        else
            str << "... ";
        str << "]";
        break;

    case tLambda: {
        std::ostringstream s;
        s << v.lambda.fun->pos;
        str << ANSI_BLUE "«lambda @ " << filterANSIEscapes(s.str()) << "»" ANSI_NORMAL;
        break;
    }

    case tPrimOp:
        str << ANSI_MAGENTA "«primop»" ANSI_NORMAL;
        break;

    case tPrimOpApp:
        str << ANSI_BLUE "«primop-app»" ANSI_NORMAL;
        break;

    case tFloat:
        str << v.fpoint;
        break;

    default:
        str << ANSI_RED "«unknown»" ANSI_NORMAL;
        break;
    }

    return str;
}

struct CmdRepl : StoreCommand, MixEvalArgs
{
    std::vector<std::string> files;

    CmdRepl()
    {
        expectArgs({
            .label = "files",
            .handler = {&files},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "start an interactive environment for evaluating Nix expressions";
    }

    Examples examples() override
    {
        return {
          Example{
            "Display all special commands within the REPL:",
              "nix repl\n  nix-repl> :?"
          }
        };
    }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = false;
        auto repl = std::make_unique<NixRepl>(searchPath, openStore());
        repl->autoArgs = getAutoArgs(*repl->state);
        repl->mainLoop(files);
    }
};

static auto r1 = registerCommand<CmdRepl>("repl");

}

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
#include "log-store.hh"
#include "common-eval-args.hh"
#include "get-drvs.hh"
#include "derivations.hh"
#include "globals.hh"
#include "command.hh"
#include "finally.hh"
#include "markdown.hh"

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
    std::string curDir;
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
    StringSet completePrefix(const std::string & prefix);
    bool getLine(std::string & input, const std::string &prompt);
    StorePath getDerivationPath(Value & v);
    bool processLine(std::string line);
    void loadFile(const Path & path);
    void loadFlake(const std::string & flakeRef);
    void initEnv();
    void reloadFiles();
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol & name, Value & v);
    Expr * parseString(std::string s);
    void evalString(std::string s, Value & v);

    typedef std::set<Value *> ValuesSeen;
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth);
    std::ostream &  printValue(std::ostream & str, Value & v, unsigned int maxDepth, ValuesSeen & seen);
};


std::string removeWhitespace(std::string s)
{
    s = chomp(s);
    size_t n = s.find_first_not_of(" \n\r\t");
    if (n != std::string::npos) s = std::string(s, n);
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

std::string runNix(Path program, const Strings & args,
    const std::optional<std::string> & input = {})
{
    auto subprocessEnv = getEnv();
    subprocessEnv["NIX_CONFIG"] = globalConfig.toKeyValue();

    auto res = runProgram(RunOptions {
        .program = settings.nixBinDir+ "/" + program,
        .args = args,
        .environment = subprocessEnv,
        .input = input,
    });

    if (!statusOk(res.first))
        throw ExecError(res.first, fmt("program '%1%' %2%", program, statusToString(res.first)));

    return res.second;
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
    std::string error = ANSI_RED "error:" ANSI_NORMAL " ";
    notice("Welcome to Nix " + nixVersion + ". Type :? for help.\n");

    for (auto & i : files)
        loadedFiles.push_back(i);

    reloadFiles();
    if (!loadedFiles.empty()) notice("");

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
            if (e.msg().find("unexpected end of file") != std::string::npos) {
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


bool NixRepl::getLine(std::string & input, const std::string & prompt)
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
    Finally resetTerminal([&]() { rl_deprep_terminal(); });
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


StringSet NixRepl::completePrefix(const std::string & prefix)
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

    if ((slash = cur.rfind('/')) != std::string::npos) {
        try {
            auto dir = std::string(cur, 0, slash);
            auto prefix2 = std::string(cur, slash + 1);
            for (auto & entry : readDirectory(dir == "" ? "/" : dir)) {
                if (entry.name[0] != '.' && hasPrefix(entry.name, prefix2))
                    completions.insert(prev + dir + "/" + entry.name);
            }
        } catch (Error &) {
        }
    } else if ((dot = cur.rfind('.')) == std::string::npos) {
        /* This is a variable name; look it up in the current scope. */
        StringSet::iterator i = varNames.lower_bound(cur);
        while (i != varNames.end()) {
            if (i->substr(0, cur.size()) != cur) break;
            completions.insert(prev + *i);
            i++;
        }
    } else {
        try {
            /* This is an expression that should evaluate to an
               attribute set.  Evaluate it to get the names of the
               attributes. */
            auto expr = cur.substr(0, dot);
            auto cur2 = cur.substr(dot + 1);

            Expr * e = parseString(expr);
            Value v;
            e->eval(*state, *env, v);
            state->forceAttrs(v, noPos);

            for (auto & i : *v.attrs) {
                std::string name = i.name;
                if (name.substr(0, cur2.size()) != cur2) continue;
                completions.insert(prev + expr + "." + name);
            }

        } catch (ParseError & e) {
            // Quietly ignore parse errors.
        } catch (EvalError & e) {
            // Quietly ignore evaluation errors.
        } catch (UndefinedVarError & e) {
            // Quietly ignore undefined variable errors.
        } catch (BadURL & e) {
            // Quietly ignore BadURL flake-related errors.
        }
    }

    return completions;
}


static bool isVarName(std::string_view s)
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


StorePath NixRepl::getDerivationPath(Value & v) {
    auto drvInfo = getDerivation(*state, v, false);
    if (!drvInfo)
        throw Error("expression does not evaluate to a derivation, so I can't build it");
    auto drvPath = drvInfo->queryDrvPath();
    if (!drvPath)
        throw Error("expression did not evaluate to a valid derivation (no 'drvPath' attribute)");
    if (!state->store->isValidPath(*drvPath))
        throw Error("expression evaluated to invalid derivation '%s'", state->store->printStorePath(*drvPath));
    return *drvPath;
}


bool NixRepl::processLine(std::string line)
{
    line = trim(line);
    if (line == "") return true;

    _isInterrupted = false;

    std::string command, arg;

    if (line[0] == ':') {
        size_t p = line.find_first_of(" \n\r\t");
        command = line.substr(0, p);
        if (p != std::string::npos) arg = removeWhitespace(line.substr(p));
    } else {
        arg = line;
    }

    if (command == ":?" || command == ":help") {
        // FIXME: convert to Markdown, include in the 'nix repl' manpage.
        std::cout
             << "The following commands are available:\n"
             << "\n"
             << "  <expr>        Evaluate and print expression\n"
             << "  <x> = <expr>  Bind expression to variable\n"
             << "  :a <expr>     Add attributes from resulting set to scope\n"
             << "  :b <expr>     Build derivation\n"
             << "  :e <expr>     Open package or function in $EDITOR\n"
             << "  :i <expr>     Build derivation, then install result into current profile\n"
             << "  :l <path>     Load Nix expression and add it to scope\n"
             << "  :lf <ref>     Load Nix flake and add it to scope\n"
             << "  :p <expr>     Evaluate and print expression recursively\n"
             << "  :q            Exit nix-repl\n"
             << "  :r            Reload all files\n"
             << "  :s <expr>     Build dependencies of derivation, then start nix-shell\n"
             << "  :t <expr>     Describe result of evaluation\n"
             << "  :u <expr>     Build derivation, then start nix-shell\n"
             << "  :doc <expr>   Show documentation of a builtin function\n"
             << "  :log <expr>   Show logs for a derivation\n"
             << "  :st [bool]    Enable, disable or toggle showing traces for errors\n";
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

    else if (command == ":lf" || command == ":load-flake") {
        loadFlake(arg);
    }

    else if (command == ":r" || command == ":reload") {
        state->resetFileCache();
        reloadFiles();
    }

    else if (command == ":e" || command == ":edit") {
        Value v;
        evalString(arg, v);

        Pos pos;

        if (v.type() == nPath || v.type() == nString) {
            PathSet context;
            auto filename = state->coerceToString(noPos, v, context);
            pos.file = state->symbols.create(*filename);
        } else if (v.isLambda()) {
            pos = v.lambda.fun->pos;
        } else {
            // assume it's a derivation
            pos = findPackageFilename(*state, v, arg);
        }

        // Open in EDITOR
        auto args = editorFor(pos);
        auto editor = args.front();
        args.pop_front();

        // runProgram redirects stdout to a StringSink,
        // using runProgram2 to allow editors to display their UI
        runProgram2(RunOptions { .program = editor, .searchPath = true, .args = args });

        // Reload right after exiting the editor
        state->resetFileCache();
        reloadFiles();
    }

    else if (command == ":t") {
        Value v;
        evalString(arg, v);
        logger->cout(showType(v));
    }

    else if (command == ":u") {
        Value v, f, result;
        evalString(arg, v);
        evalString("drv: (import <nixpkgs> {}).runCommand \"shell\" { buildInputs = [ drv ]; } \"\"", f);
        state->callFunction(f, v, result, Pos());

        StorePath drvPath = getDerivationPath(result);
        runNix("nix-shell", {state->store->printStorePath(drvPath)});
    }

    else if (command == ":b" || command == ":i" || command == ":s" || command == ":log") {
        Value v;
        evalString(arg, v);
        StorePath drvPath = getDerivationPath(v);
        Path drvPathRaw = state->store->printStorePath(drvPath);

        if (command == ":b") {
            state->store->buildPaths({DerivedPath::Built{drvPath}});
            auto drv = state->store->readDerivation(drvPath);
            logger->cout("\nThis derivation produced the following outputs:");
            for (auto & [outputName, outputPath] : state->store->queryDerivationOutputMap(drvPath))
                logger->cout("  %s -> %s", outputName, state->store->printStorePath(outputPath));
        } else if (command == ":i") {
            runNix("nix-env", {"-i", drvPathRaw});
        } else if (command == ":log") {
            settings.readOnlyMode = true;
            Finally roModeReset([&]() {
                settings.readOnlyMode = false;
            });
            auto subs = getDefaultSubstituters();

            subs.push_front(state->store);

            bool foundLog = false;
            RunPager pager;
            for (auto & sub : subs) {
                auto * logSubP = dynamic_cast<LogStore *>(&*sub);
                if (!logSubP) {
                    printInfo("Skipped '%s' which does not support retrieving build logs", sub->getUri());
                    continue;
                }
                auto & logSub = *logSubP;

                auto log = logSub.getBuildLog(drvPath);
                if (log) {
                    printInfo("got build log for '%s' from '%s'", drvPathRaw, logSub.getUri());
                    logger->writeToStdout(*log);
                    foundLog = true;
                    break;
                }
            }
            if (!foundLog) throw Error("build log of '%s' is not available", drvPathRaw);
        } else {
            runNix("nix-shell", {drvPathRaw});
        }
    }

    else if (command == ":p" || command == ":print") {
        Value v;
        evalString(arg, v);
        printValue(std::cout, v, 1000000000) << std::endl;
    }

    else if (command == ":q" || command == ":quit")
        return false;

    else if (command == ":doc") {
        Value v;
        evalString(arg, v);
        if (auto doc = state->getDoc(v)) {
            std::string markdown;

            if (!doc->args.empty() && doc->name) {
                auto args = doc->args;
                for (auto & arg : args)
                    arg = "*" + arg + "*";

                markdown +=
                    "**Synopsis:** `builtins." + (std::string) (*doc->name) + "` "
                    + concatStringsSep(" ", args) + "\n\n";
            }

            markdown += stripIndentation(doc->doc);

            logger->cout(trim(renderMarkdownToTerminal(markdown)));
        } else
            throw Error("value does not have documentation");
    }

    else if (command == ":st" || command == ":show-trace") {
        if (arg == "false" || (arg == "" && loggerSettings.showTrace)) {
            std::cout << "not showing error traces\n";
            loggerSettings.showTrace = false;
        } else if (arg == "true" || (arg == "" && !loggerSettings.showTrace)) {
            std::cout << "showing error traces\n";
            loggerSettings.showTrace = true;
        } else {
            throw Error("unexpected argument '%s' to %s", arg, command);
        };
    }

    else if (command != "")
        throw Error("unknown command '%1%'", command);

    else {
        size_t p = line.find('=');
        std::string name;
        if (p != std::string::npos &&
            p < line.size() &&
            line[p + 1] != '=' &&
            isVarName(name = removeWhitespace(line.substr(0, p))))
        {
            Expr * e = parseString(line.substr(p + 1));
            Value & v(*state->allocValue());
            v.mkThunk(env, e);
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

void NixRepl::loadFlake(const std::string & flakeRefS)
{
    if (flakeRefS.empty())
        throw Error("cannot use ':load-flake' without a path specified. (Use '.' for the current working directory.)");

    auto flakeRef = parseFlakeRef(flakeRefS, absPath("."), true);
    if (evalSettings.pureEval && !flakeRef.input.isLocked())
        throw Error("cannot use ':load-flake' on locked flake reference '%s' (use --impure to override)", flakeRefS);

    Value v;

    flake::callFlake(*state,
        flake::lockFlake(*state, flakeRef,
            flake::LockFlags {
                .updateLockFile = false,
                .useRegistries = !evalSettings.pureEval,
                .allowMutable  = !evalSettings.pureEval,
            }),
        v);
    addAttrsToScope(v);
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
        if (!first) notice("");
        first = false;
        notice("Loading '%1%'...", i);
        loadFile(i);
    }
}


void NixRepl::addAttrsToScope(Value & attrs)
{
    state->forceAttrs(attrs, [&]() { return attrs.determinePos(noPos); });
    if (displ + attrs.attrs->size() >= envSize)
        throw Error("environment full; cannot add more variables");

    for (auto & i : *attrs.attrs) {
        staticEnv.vars.emplace_back(i.name, displ);
        env->values[displ++] = i.value;
        varNames.insert((std::string) i.name);
    }
    staticEnv.sort();
    staticEnv.deduplicate();
    notice("Added %1% variables.", attrs.attrs->size());
}


void NixRepl::addVarToScope(const Symbol & name, Value & v)
{
    if (displ >= envSize)
        throw Error("environment full; cannot add more variables");
    if (auto oldVar = staticEnv.find(name); oldVar != staticEnv.vars.end())
        staticEnv.vars.erase(oldVar);
    staticEnv.vars.emplace_back(name, displ);
    staticEnv.sort();
    env->values[displ++] = &v;
    varNames.insert((std::string) name);
}


Expr * NixRepl::parseString(std::string s)
{
    Expr * e = state->parseExprFromString(std::move(s), curDir, staticEnv);
    return e;
}


void NixRepl::evalString(std::string s, Value & v)
{
    Expr * e = parseString(s);
    e->eval(*state, *env, v);
    state->forceValue(v, [&]() { return v.determinePos(noPos); });
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

    state->forceValue(v, [&]() { return v.determinePos(noPos); });

    switch (v.type()) {

    case nInt:
        str << ANSI_CYAN << v.integer << ANSI_NORMAL;
        break;

    case nBool:
        str << ANSI_CYAN << (v.boolean ? "true" : "false") << ANSI_NORMAL;
        break;

    case nString:
        str << ANSI_WARNING;
        printStringValue(str, v.string.s);
        str << ANSI_NORMAL;
        break;

    case nPath:
        str << ANSI_GREEN << v.path << ANSI_NORMAL; // !!! escaping?
        break;

    case nNull:
        str << ANSI_CYAN "null" ANSI_NORMAL;
        break;

    case nAttrs: {
        seen.insert(&v);

        bool isDrv = state->isDerivation(v);

        if (isDrv) {
            str << "«derivation ";
            Bindings::iterator i = v.attrs->find(state->sDrvPath);
            PathSet context;
            if (i != v.attrs->end())
                str << state->store->printStorePath(state->coerceToStorePath(*i->pos, *i->value, context));
            else
                str << "???";
            str << "»";
        }

        else if (maxDepth > 0) {
            str << "{ ";

            typedef std::map<std::string, Value *> Sorted;
            Sorted sorted;
            for (auto & i : *v.attrs)
                sorted[i.name] = i.value;

            for (auto & i : sorted) {
                if (isVarName(i.first))
                    str << i.first;
                else
                    printStringValue(str, i.first.c_str());
                str << " = ";
                if (seen.count(i.second))
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

    case nList:
        seen.insert(&v);

        str << "[ ";
        if (maxDepth > 0)
            for (auto elem : v.listItems()) {
                if (seen.count(elem))
                    str << "«repeated»";
                else
                    try {
                        printValue(str, *elem, maxDepth - 1, seen);
                    } catch (AssertionError & e) {
                        str << ANSI_RED "«error: " << e.msg() << "»" ANSI_NORMAL;
                    }
                str << " ";
            }
        else
            str << "... ";
        str << "]";
        break;

    case nFunction:
        if (v.isLambda()) {
            std::ostringstream s;
            s << v.lambda.fun->pos;
            str << ANSI_BLUE "«lambda @ " << filterANSIEscapes(s.str()) << "»" ANSI_NORMAL;
        } else if (v.isPrimOp()) {
            str << ANSI_MAGENTA "«primop»" ANSI_NORMAL;
        } else if (v.isPrimOpApp()) {
            str << ANSI_BLUE "«primop-app»" ANSI_NORMAL;
        } else {
            abort();
        }
        break;

    case nFloat:
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

    std::string doc() override
    {
        return
          #include "repl.md"
          ;
    }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = false;
        auto repl = std::make_unique<NixRepl>(searchPath, openStore());
        repl->autoArgs = getAutoArgs(*repl->state);
        repl->mainLoop(files);
    }
};

static auto rCmdRepl = registerCommand<CmdRepl>("repl");

}

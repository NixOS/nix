#include <iostream>
#include <cstdlib>
#include <cstring>

#include "nix/util/error.hh"
#include "nix/cmd/repl-interacter.hh"
#include "nix/cmd/repl.hh"

#include "nix/util/ansicolor.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/attr-path.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/log-store.hh"
#include "nix/cmd/common-eval-args.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/store/derivations.hh"
#include "nix/store/globals.hh"
#include "nix/flake/flake.hh"
#include "nix/flake/lockfile.hh"
#include "nix/util/users.hh"
#include "nix/cmd/editor-for.hh"
#include "nix/util/finally.hh"
#include "nix/cmd/markdown.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/expr/print.hh"
#include "nix/util/ref.hh"
#include "nix/expr/value.hh"

#include "nix/util/strings.hh"

namespace nix {

/**
 * Returned by `NixRepl::processLine`.
 */
enum class ProcessLineResult {
    /**
     * The user exited with `:quit`. The REPL should exit. The surrounding
     * program or evaluation (e.g., if the REPL was acting as the debugger)
     * should also exit.
     */
    Quit,
    /**
     * The user exited with `:continue`. The REPL should exit, but the program
     * should continue running.
     */
    Continue,
    /**
     * The user did not exit. The REPL should request another line of input.
     */
    PromptAgain,
};

struct NixRepl : AbstractNixRepl, detail::ReplCompleterMixin, gc
{
    size_t debugTraceIndex;

    // Arguments passed to :load, saved so they can be reloaded with :reload
    Strings loadedFiles;
    // Arguments passed to :load-flake, saved so they can be reloaded with :reload
    Strings loadedFlakes;
    std::function<AnnotatedValues()> getValues;

    const static int envSize = 32768;
    std::shared_ptr<StaticEnv> staticEnv;
    Value lastLoaded;
    Env * env;
    int displ;
    StringSet varNames;

    RunNix * runNixPtr;

    void runNix(Path program, const Strings & args, const std::optional<std::string> & input = {});

    std::unique_ptr<ReplInteracter> interacter;

    NixRepl(
        const LookupPath & lookupPath,
        nix::ref<Store> store,
        ref<EvalState> state,
        std::function<AnnotatedValues()> getValues,
        RunNix * runNix);
    virtual ~NixRepl() = default;

    ReplExitStatus mainLoop() override;
    void initEnv() override;

    virtual StringSet completePrefix(const std::string & prefix) override;
    StorePath getDerivationPath(Value & v);
    ProcessLineResult processLine(std::string line);

    void loadFile(const Path & path);
    void loadFlake(const std::string & flakeRef);
    void loadFiles();
    void loadFlakes();
    void reloadFilesAndFlakes();
    void showLastLoaded();
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol name, Value & v);
    Expr * parseString(std::string s);
    void evalString(std::string s, Value & v);
    void loadDebugTraceEnv(DebugTrace & dt);

    void printValue(std::ostream & str, Value & v, unsigned int maxDepth = std::numeric_limits<unsigned int>::max())
    {
        // Hide the progress bar during printing because it might interfere
        auto suspension = logger->suspend();
        ::nix::printValue(
            *state,
            str,
            v,
            PrintOptions{
                .ansiColors = true,
                .force = true,
                .derivationPaths = true,
                .maxDepth = maxDepth,
                .prettyIndent = 2,
                .errors = ErrorPrintBehavior::ThrowTopLevel,
            });
    }
};

std::string removeWhitespace(std::string s)
{
    s = chomp(s);
    size_t n = s.find_first_not_of(" \n\r\t");
    if (n != std::string::npos)
        s = std::string(s, n);
    return s;
}

NixRepl::NixRepl(
    const LookupPath & lookupPath,
    nix::ref<Store> store,
    ref<EvalState> state,
    std::function<NixRepl::AnnotatedValues()> getValues,
    RunNix * runNix)
    : AbstractNixRepl(state)
    , debugTraceIndex(0)
    , getValues(getValues)
    , staticEnv(new StaticEnv(nullptr, state->staticBaseEnv))
    , runNixPtr{runNix}
    , interacter(make_unique<ReadlineLikeInteracter>(getDataDir() + "/repl-history"))
{
}

static std::ostream & showDebugTrace(std::ostream & out, const PosTable & positions, const DebugTrace & dt)
{
    if (dt.isError)
        out << ANSI_RED "error: " << ANSI_NORMAL;
    out << dt.hint.str() << "\n";

    auto pos = dt.getPos(positions);

    if (pos) {
        out << pos;
        if (auto loc = pos.getCodeLines()) {
            out << "\n";
            printCodeLines(out, "", pos, *loc);
            out << "\n";
        }
    }

    return out;
}

MakeError(IncompleteReplExpr, ParseError);

static bool isFirstRepl = true;

ReplExitStatus NixRepl::mainLoop()
{
    if (isFirstRepl) {
        std::string_view debuggerNotice = "";
        if (state->debugRepl) {
            debuggerNotice = " debugger";
        }
        notice("Nix %1%%2%\nType :? for help.", nixVersion, debuggerNotice);
    }

    isFirstRepl = false;

    loadFiles();

    auto _guard = interacter->init(static_cast<detail::ReplCompleterMixin *>(this));

    std::string input;

    while (true) {
        // Hide the progress bar while waiting for user input, so that it won't interfere.
        {
            auto suspension = logger->suspend();
            // When continuing input from previous lines, don't print a prompt, just align to the same
            // number of chars as the prompt.
            if (!interacter->getLine(
                    input, input.empty() ? ReplPromptType::ReplPrompt : ReplPromptType::ContinuationPrompt)) {
                // Ctrl-D should exit the debugger.
                state->debugStop = false;
                logger->cout("");
                // TODO: Should Ctrl-D exit just the current debugger session or
                // the entire program?
                return ReplExitStatus::QuitAll;
            }
            // `suspension` resumes the logger
        }
        try {
            switch (processLine(input)) {
            case ProcessLineResult::Quit:
                return ReplExitStatus::QuitAll;
            case ProcessLineResult::Continue:
                return ReplExitStatus::Continue;
            case ProcessLineResult::PromptAgain:
                break;
            default:
                unreachable();
            }
        } catch (IncompleteReplExpr &) {
            continue;
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
            for (auto & entry : DirectoryIterator{dir == "" ? "/" : dir}) {
                checkInterrupt();
                auto name = entry.path().filename().string();
                if (name[0] != '.' && hasPrefix(name, prefix2))
                    completions.insert(prev + entry.path().string());
            }
        } catch (Error &) {
        }
    } else if ((dot = cur.rfind('.')) == std::string::npos) {
        /* This is a variable name; look it up in the current scope. */
        StringSet::iterator i = varNames.lower_bound(cur);
        while (i != varNames.end()) {
            if (i->substr(0, cur.size()) != cur)
                break;
            completions.insert(prev + *i);
            i++;
        }
    } else {
        /* Temporarily disable the debugger, to avoid re-entering readline. */
        auto debug_repl = state->debugRepl;
        state->debugRepl = nullptr;
        Finally restoreDebug([&]() { state->debugRepl = debug_repl; });
        try {
            /* This is an expression that should evaluate to an
               attribute set.  Evaluate it to get the names of the
               attributes. */
            auto expr = cur.substr(0, dot);
            auto cur2 = cur.substr(dot + 1);

            Expr * e = parseString(expr);
            Value v;
            e->eval(*state, *env, v);
            state->forceAttrs(
                v,
                noPos,
                "while evaluating an attrset for the purpose of completion (this error should not be displayed; file an issue?)");

            for (auto & i : *v.attrs()) {
                std::string_view name = state->symbols[i.name];
                if (name.substr(0, cur2.size()) != cur2)
                    continue;
                completions.insert(concatStrings(prev, expr, ".", name));
            }

        } catch (ParseError & e) {
            // Quietly ignore parse errors.
        } catch (EvalError & e) {
            // Quietly ignore evaluation errors.
        } catch (BadURL & e) {
            // Quietly ignore BadURL flake-related errors.
        } catch (FileNotFound & e) {
            // Quietly ignore non-existent file being `import`-ed.
        }
    }

    return completions;
}

// FIXME: DRY and match or use the parser
static bool isVarName(std::string_view s)
{
    if (s.size() == 0)
        return false;
    char c = s[0];
    if ((c >= '0' && c <= '9') || c == '-' || c == '\'')
        return false;
    for (auto & i : s)
        if (!((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') || (i >= '0' && i <= '9') || i == '_' || i == '-'
              || i == '\''))
            return false;
    return true;
}

StorePath NixRepl::getDerivationPath(Value & v)
{
    auto packageInfo = getDerivation(*state, v, false);
    if (!packageInfo)
        throw Error("expression does not evaluate to a derivation, so I can't build it");
    auto drvPath = packageInfo->queryDrvPath();
    if (!drvPath)
        throw Error("expression did not evaluate to a valid derivation (no 'drvPath' attribute)");
    if (!state->store->isValidPath(*drvPath))
        throw Error("expression evaluated to invalid derivation '%s'", state->store->printStorePath(*drvPath));
    return *drvPath;
}

void NixRepl::loadDebugTraceEnv(DebugTrace & dt)
{
    initEnv();

    auto se = state->getStaticEnv(dt.expr);
    if (se) {
        auto vm = mapStaticEnvBindings(state->symbols, *se.get(), dt.env);

        // add staticenv vars.
        for (auto & [name, value] : *(vm.get()))
            addVarToScope(state->symbols.create(name), *value);
    }
}

ProcessLineResult NixRepl::processLine(std::string line)
{
    line = trim(line);
    if (line.empty())
        return ProcessLineResult::PromptAgain;

    setInterrupted(false);

    std::string command, arg;

    if (line[0] == ':') {
        size_t p = line.find_first_of(" \n\r\t");
        command = line.substr(0, p);
        if (p != std::string::npos)
            arg = removeWhitespace(line.substr(p));
    } else {
        arg = line;
    }

    if (command == ":?" || command == ":help") {
        // FIXME: convert to Markdown, include in the 'nix repl' manpage.
        std::cout << "The following commands are available:\n"
                  << "\n"
                  << "  <expr>                       Evaluate and print expression\n"
                  << "  <x> = <expr>                 Bind expression to variable\n"
                  << "  :a, :add <expr>              Add attributes from resulting set to scope\n"
                  << "  :b <expr>                    Build a derivation\n"
                  << "  :bl <expr>                   Build a derivation, creating GC roots in the\n"
                  << "                               working directory\n"
                  << "  :e, :edit <expr>             Open package or function in $EDITOR\n"
                  << "  :i <expr>                    Build derivation, then install result into\n"
                  << "                               current profile\n"
                  << "  :l, :load <path>             Load Nix expression and add it to scope\n"
                  << "  :lf, :load-flake <ref>       Load Nix flake and add it to scope\n"
                  << "  :ll, :last-loaded            Show most recently loaded variables added to scope\n"
                  << "  :p, :print <expr>            Evaluate and print expression recursively\n"
                  << "                               Strings are printed directly, without escaping.\n"
                  << "  :q, :quit                    Exit nix-repl\n"
                  << "  :r, :reload                  Reload all files\n"
                  << "  :sh <expr>                   Build dependencies of derivation, then start\n"
                  << "                               nix-shell\n"
                  << "  :t <expr>                    Describe result of evaluation\n"
                  << "  :u <expr>                    Build derivation, then start nix-shell\n"
                  << "  :doc <expr>                  Show documentation of a builtin function\n"
                  << "  :log <expr>                  Show logs for a derivation\n"
                  << "  :te, :trace-enable [bool]    Enable, disable or toggle showing traces for\n"
                  << "                               errors\n"
                  << "  :?, :help                    Brings up this help menu\n";
        if (state->debugRepl) {
            std::cout << "\n"
                      << "        Debug mode commands\n"
                      << "  :env             Show env stack\n"
                      << "  :bt, :backtrace  Show trace stack\n"
                      << "  :st              Show current trace\n"
                      << "  :st <idx>        Change to another trace in the stack\n"
                      << "  :c, :continue    Go until end of program, exception, or builtins.break\n"
                      << "  :s, :step        Go one step\n";
        }

    }

    else if (state->debugRepl && (command == ":bt" || command == ":backtrace")) {
        for (const auto & [idx, i] : enumerate(state->debugTraces)) {
            std::cout << "\n" << ANSI_BLUE << idx << ANSI_NORMAL << ": ";
            showDebugTrace(std::cout, state->positions, i);
        }
    }

    else if (state->debugRepl && (command == ":env")) {
        for (const auto & [idx, i] : enumerate(state->debugTraces)) {
            if (idx == debugTraceIndex) {
                printEnvBindings(*state, i.expr, i.env);
                break;
            }
        }
    }

    else if (state->debugRepl && (command == ":st")) {
        try {
            // change the DebugTrace index.
            debugTraceIndex = stoi(arg);
        } catch (...) {
        }

        for (const auto & [idx, i] : enumerate(state->debugTraces)) {
            if (idx == debugTraceIndex) {
                std::cout << "\n" << ANSI_BLUE << idx << ANSI_NORMAL << ": ";
                showDebugTrace(std::cout, state->positions, i);
                std::cout << std::endl;
                printEnvBindings(*state, i.expr, i.env);
                loadDebugTraceEnv(i);
                break;
            }
        }
    }

    else if (state->debugRepl && (command == ":s" || command == ":step")) {
        // set flag to stop at next DebugTrace; exit repl.
        state->debugStop = true;
        return ProcessLineResult::Continue;
    }

    else if (state->debugRepl && (command == ":c" || command == ":continue")) {
        // set flag to run to next breakpoint or end of program; exit repl.
        state->debugStop = false;
        return ProcessLineResult::Continue;
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

    else if (command == ":ll" || command == ":last-loaded") {
        showLastLoaded();
    }

    else if (command == ":r" || command == ":reload") {
        state->resetFileCache();
        reloadFilesAndFlakes();
    }

    else if (command == ":e" || command == ":edit") {
        Value v;
        evalString(arg, v);

        const auto [path, line] = [&]() -> std::pair<SourcePath, uint32_t> {
            if (v.type() == nPath || v.type() == nString) {
                NixStringContext context;
                auto path = state->coerceToPath(noPos, v, context, "while evaluating the filename to edit");
                return {path, 0};
            } else if (v.isLambda()) {
                auto pos = state->positions[v.lambda().fun->pos];
                if (auto path = std::get_if<SourcePath>(&pos.origin))
                    return {*path, pos.line};
                else
                    throw Error("'%s' cannot be shown in an editor", pos);
            } else {
                // assume it's a derivation
                return findPackageFilename(*state, v, arg);
            }
        }();

        // Open in EDITOR
        auto args = editorFor(path, line);
        auto editor = args.front();
        args.pop_front();

        // runProgram redirects stdout to a StringSink,
        // using runProgram2 to allow editors to display their UI
        runProgram2(RunOptions{.program = editor, .lookupPath = true, .args = args, .isInteractive = true});

        // Reload right after exiting the editor
        state->resetFileCache();
        reloadFilesAndFlakes();
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
        state->callFunction(f, v, result, PosIdx());

        StorePath drvPath = getDerivationPath(result);
        runNix("nix-shell", {state->store->printStorePath(drvPath)});
    }

    else if (command == ":b" || command == ":bl" || command == ":i" || command == ":sh" || command == ":log") {
        Value v;
        evalString(arg, v);
        StorePath drvPath = getDerivationPath(v);
        Path drvPathRaw = state->store->printStorePath(drvPath);

        if (command == ":b" || command == ":bl") {
            state->store->buildPaths({
                DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(drvPath),
                    .outputs = OutputsSpec::All{},
                },
            });
            auto drv = state->store->readDerivation(drvPath);
            logger->cout("\nThis derivation produced the following outputs:");
            for (auto & [outputName, outputPath] : state->store->queryDerivationOutputMap(drvPath)) {
                auto localStore = state->store.dynamic_pointer_cast<LocalFSStore>();
                if (localStore && command == ":bl") {
                    std::string symlink = "repl-result-" + outputName;
                    localStore->addPermRoot(outputPath, absPath(symlink));
                    logger->cout("  ./%s -> %s", symlink, state->store->printStorePath(outputPath));
                } else {
                    logger->cout("  %s -> %s", outputName, state->store->printStorePath(outputPath));
                }
            }
        } else if (command == ":i") {
            runNix("nix-env", {"-i", drvPathRaw});
        } else if (command == ":log") {
            settings.readOnlyMode = true;
            Finally roModeReset([&]() { settings.readOnlyMode = false; });
            auto subs = getDefaultSubstituters();

            subs.push_front(state->store);

            bool foundLog = false;
            RunPager pager;
            for (auto & sub : subs) {
                auto * logSubP = dynamic_cast<LogStore *>(&*sub);
                if (!logSubP) {
                    printInfo(
                        "Skipped '%s' which does not support retrieving build logs", sub->config.getHumanReadableURI());
                    continue;
                }
                auto & logSub = *logSubP;

                auto log = logSub.getBuildLog(drvPath);
                if (log) {
                    printInfo("got build log for '%s' from '%s'", drvPathRaw, logSub.config.getHumanReadableURI());
                    logger->writeToStdout(*log);
                    foundLog = true;
                    break;
                }
            }
            if (!foundLog)
                throw Error("build log of '%s' is not available", drvPathRaw);
        } else {
            runNix("nix-shell", {drvPathRaw});
        }
    }

    else if (command == ":p" || command == ":print") {
        Value v;
        evalString(arg, v);
        auto suspension = logger->suspend();
        if (v.type() == nString) {
            std::cout << v.string_view();
        } else {
            printValue(std::cout, v);
        }
        std::cout << std::endl;
    }

    else if (command == ":q" || command == ":quit") {
        state->debugStop = false;
        return ProcessLineResult::Quit;
    }

    else if (command == ":doc") {
        Value v;

        auto expr = parseString(arg);
        std::string fallbackName;
        PosIdx fallbackPos;
        DocComment fallbackDoc;
        if (auto select = dynamic_cast<ExprSelect *>(expr)) {
            Value vAttrs;
            auto name = select->evalExceptFinalSelect(*state, *env, vAttrs);
            fallbackName = state->symbols[name];

            state->forceAttrs(vAttrs, noPos, "while evaluating an attribute set to look for documentation");
            auto attrs = vAttrs.attrs();
            assert(attrs);
            auto attr = attrs->get(name);
            if (!attr) {
                // When missing, trigger the normal exception
                // e.g. :doc builtins.foo
                // behaves like
                // nix-repl> builtins.foo<tab>
                // error: attribute 'foo' missing
                evalString(arg, v);
                assert(false);
            }
            if (attr->pos) {
                fallbackPos = attr->pos;
                fallbackDoc = state->getDocCommentForPos(fallbackPos);
            }
        }

        evalString(arg, v);
        if (auto doc = state->getDoc(v)) {
            std::string markdown;

            if (!doc->args.empty() && doc->name) {
                auto args = doc->args;
                for (auto & arg : args)
                    arg = "*" + arg + "*";

                markdown += "**Synopsis:** `builtins." + (std::string) (*doc->name) + "` " + concatStringsSep(" ", args)
                            + "\n\n";
            }

            markdown += stripIndentation(doc->doc);

            logger->cout(trim(renderMarkdownToTerminal(markdown)));
        } else if (fallbackPos) {
            std::ostringstream ss;
            ss << "Attribute `" << fallbackName << "`\n\n";
            ss << "  … defined at " << state->positions[fallbackPos] << "\n\n";
            if (fallbackDoc) {
                ss << fallbackDoc.getInnerText(state->positions);
            } else {
                ss << "No documentation found.\n\n";
            }

            auto markdown = ss.view();
            logger->cout(trim(renderMarkdownToTerminal(markdown)));

        } else
            throw Error("value does not have documentation");
    }

    else if (command == ":te" || command == ":trace-enable") {
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
        if (p != std::string::npos && p < line.size() && line[p + 1] != '='
            && isVarName(name = removeWhitespace(line.substr(0, p)))) {
            Expr * e = parseString(line.substr(p + 1));
            Value & v(*state->allocValue());
            v.mkThunk(env, e);
            addVarToScope(state->symbols.create(name), v);
        } else {
            Value v;
            evalString(line, v);
            auto suspension = logger->suspend();
            printValue(std::cout, v, 1);
            std::cout << std::endl;
        }
    }

    return ProcessLineResult::PromptAgain;
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

    loadedFlakes.remove(flakeRefS);
    loadedFlakes.push_back(flakeRefS);

    std::filesystem::path cwd;
    try {
        cwd = std::filesystem::current_path();
    } catch (std::filesystem::filesystem_error & e) {
        throw SysError("cannot determine current working directory");
    }

    auto flakeRef = parseFlakeRef(fetchSettings, flakeRefS, cwd.string(), true);
    if (evalSettings.pureEval && !flakeRef.input.isLocked())
        throw Error("cannot use ':load-flake' on locked flake reference '%s' (use --impure to override)", flakeRefS);

    Value v;

    flake::callFlake(
        *state,
        flake::lockFlake(
            flakeSettings,
            *state,
            flakeRef,
            flake::LockFlags{
                .updateLockFile = false,
                .useRegistries = !evalSettings.pureEval,
                .allowUnlocked = !evalSettings.pureEval,
            }),
        v);
    addAttrsToScope(v);
}

void NixRepl::initEnv()
{
    env = &state->mem.allocEnv(envSize);
    env->up = &state->baseEnv;
    displ = 0;
    staticEnv->vars.clear();

    varNames.clear();
    for (auto & i : state->staticBaseEnv->vars)
        varNames.emplace(state->symbols[i.first]);
}

void NixRepl::showLastLoaded()
{
    RunPager pager;

    for (auto & i : *lastLoaded.attrs()) {
        std::string_view name = state->symbols[i.name];
        logger->cout(name);
    }
}

void NixRepl::reloadFilesAndFlakes()
{
    initEnv();

    loadFiles();
    loadFlakes();
}

void NixRepl::loadFiles()
{
    Strings old = loadedFiles;
    loadedFiles.clear();

    for (auto & i : old) {
        notice("Loading '%1%'...", i);
        loadFile(i);
    }

    for (auto & [i, what] : getValues()) {
        notice("Loading installable '%1%'...", what);
        addAttrsToScope(*i);
    }
}

void NixRepl::loadFlakes()
{
    Strings old = loadedFlakes;
    loadedFlakes.clear();

    for (auto & i : old) {
        notice("Loading flake '%1%'...", i);
        loadFlake(i);
    }
}

void NixRepl::addAttrsToScope(Value & attrs)
{
    state->forceAttrs(
        attrs,
        [&]() { return attrs.determinePos(noPos); },
        "while evaluating an attribute set to be merged in the global scope");
    if (displ + attrs.attrs()->size() >= envSize)
        throw Error("environment full; cannot add more variables");

    for (auto & i : *attrs.attrs()) {
        staticEnv->vars.emplace_back(i.name, displ);
        env->values[displ++] = i.value;
        varNames.emplace(state->symbols[i.name]);
    }
    staticEnv->sort();
    staticEnv->deduplicate();
    notice("Added %1% variables.", attrs.attrs()->size());

    lastLoaded = attrs;

    const int max_print = 20;
    int counter = 0;
    std::ostringstream loaded;
    for (auto & i : attrs.attrs()->lexicographicOrder(state->symbols)) {
        if (counter >= max_print)
            break;

        if (counter > 0)
            loaded << ", ";

        printIdentifier(loaded, state->symbols[i->name]);
        counter += 1;
    }

    notice("%1%", loaded.str());

    if (attrs.attrs()->size() > max_print)
        notice("... and %1% more; view with :ll", attrs.attrs()->size() - max_print);
}

void NixRepl::addVarToScope(const Symbol name, Value & v)
{
    if (displ >= envSize)
        throw Error("environment full; cannot add more variables");
    if (auto oldVar = staticEnv->find(name); oldVar != staticEnv->vars.end())
        staticEnv->vars.erase(oldVar);
    staticEnv->vars.emplace_back(name, displ);
    staticEnv->sort();
    env->values[displ++] = &v;
    varNames.emplace(state->symbols[name]);
}

Expr * NixRepl::parseString(std::string s)
{
    try {
        return state->parseExprFromString(std::move(s), state->rootPath("."), staticEnv);
    } catch (ParseError & e) {
        if (e.msg().find("unexpected end of file") != std::string::npos)
            // For parse errors on incomplete input, we continue waiting for the next line of
            // input without clearing the input so far.
            throw IncompleteReplExpr(e.msg());
        else
            throw;
    }
}

void NixRepl::evalString(std::string s, Value & v)
{
    Expr * e = parseString(s);
    e->eval(*state, *env, v);
    state->forceValue(v, v.determinePos(noPos));
}

void NixRepl::runNix(Path program, const Strings & args, const std::optional<std::string> & input)
{
    if (runNixPtr)
        (*runNixPtr)(program, args, input);
    else
        throw Error(
            "Cannot run '%s' because no method of calling the Nix CLI was provided. This is a configuration problem pertaining to how this program was built. See Nix 2.25 release notes",
            program);
}

std::unique_ptr<AbstractNixRepl> AbstractNixRepl::create(
    const LookupPath & lookupPath,
    nix::ref<Store> store,
    ref<EvalState> state,
    std::function<AnnotatedValues()> getValues,
    RunNix * runNix)
{
    return std::make_unique<NixRepl>(lookupPath, std::move(store), state, getValues, runNix);
}

ReplExitStatus AbstractNixRepl::runSimple(ref<EvalState> evalState, const ValMap & extraEnv)
{
    auto getValues = [&]() -> NixRepl::AnnotatedValues {
        NixRepl::AnnotatedValues values;
        return values;
    };
    LookupPath lookupPath = {};
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDelete)
    auto repl = std::make_unique<NixRepl>(
        lookupPath,
        openStore(),
        evalState,
        getValues,
        /*runNix=*/nullptr);

    repl->initEnv();

    // add 'extra' vars.
    for (auto & [name, value] : extraEnv)
        repl->addVarToScope(repl->state->symbols.create(name), *value);

    return repl->mainLoop();
}

} // namespace nix

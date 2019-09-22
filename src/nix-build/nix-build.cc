#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

#include "store-api.hh"
#include "globals.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "util.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "get-drvs.hh"
#include "common-eval-args.hh"
#include "attr-path.hh"
#include "legacy.hh"

using namespace nix;
using namespace std::string_literals;

extern char * * environ;

/* Recreate the effect of the perl shellwords function, breaking up a
 * string into arguments like a shell word, including escapes
 */
std::vector<string> shellwords(const string & s)
{
    std::regex whitespace("^(\\s+).*");
    auto begin = s.cbegin();
    std::vector<string> res;
    std::string cur;
    enum state {
        sBegin,
        sQuote
    };
    state st = sBegin;
    auto it = begin;
    for (; it != s.cend(); ++it) {
        if (st == sBegin) {
            std::smatch match;
            if (regex_search(it, s.cend(), match, whitespace)) {
                cur.append(begin, it);
                res.push_back(cur);
                cur.clear();
                it = match[1].second;
                begin = it;
            }
        }
        switch (*it) {
            case '"':
                cur.append(begin, it);
                begin = it + 1;
                st = st == sBegin ? sQuote : sBegin;
                break;
            case '\\':
                /* perl shellwords mostly just treats the next char as part of the string with no special processing */
                cur.append(begin, it);
                begin = ++it;
                break;
        }
    }
    cur.append(begin, it);
    if (!cur.empty()) res.push_back(cur);
    return res;
}

static void _main(int argc, char * * argv)
{
    auto dryRun = false;
    auto runEnv = std::regex_search(argv[0], std::regex("nix-shell$"));
    auto pure = false;
    auto fromArgs = false;
    auto packages = false;
    // Same condition as bash uses for interactive shells
    auto interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
    Strings attrPaths;
    Strings left;
    RepairFlag repair = NoRepair;
    Path gcRoot;
    BuildMode buildMode = bmNormal;
    bool readStdin = false;

    std::string envCommand; // interactive shell
    Strings envExclude;

    auto myName = runEnv ? "nix-shell" : "nix-build";

    auto inShebang = false;
    std::string script;
    std::vector<string> savedArgs;

    AutoDelete tmpDir(createTempDir("", myName));

    std::string outLink = "./result";

    // List of environment variables kept for --pure
    std::set<string> keepVars{"HOME", "USER", "LOGNAME", "DISPLAY", "PATH", "TERM", "IN_NIX_SHELL", "TZ", "PAGER", "NIX_BUILD_SHELL", "SHLVL"};

    Strings args;
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);

    // Heuristic to see if we're invoked as a shebang script, namely,
    // if we have at least one argument, it's the name of an
    // executable file, and it starts with "#!".
    if (runEnv && argc > 1 && !std::regex_search(argv[1], std::regex("nix-shell"))) {
        script = argv[1];
        try {
            auto lines = tokenizeString<Strings>(readFile(script), "\n");
            if (std::regex_search(lines.front(), std::regex("^#!"))) {
                lines.pop_front();
                inShebang = true;
                for (int i = 2; i < argc; ++i)
                    savedArgs.push_back(argv[i]);
                args.clear();
                for (auto line : lines) {
                    line = chomp(line);
                    std::smatch match;
                    if (std::regex_match(line, match, std::regex("^#!\\s*nix-shell (.*)$")))
                        for (const auto & word : shellwords(match[1].str()))
                            args.push_back(word);
                }
            }
        } catch (SysError &) { }
    }

    struct MyArgs : LegacyArgs, MixEvalArgs
    {
        using LegacyArgs::LegacyArgs;
    };

    MyArgs myArgs(myName, [&](Strings::iterator & arg, const Strings::iterator & end) {
        if (*arg == "--help") {
            deletePath(tmpDir);
            showManPage(myName);
        }

        else if (*arg == "--version")
            printVersion(myName);

        else if (*arg == "--add-drv-link" || *arg == "--indirect")
            ; // obsolete

        else if (*arg == "--no-out-link" || *arg == "--no-link")
            outLink = (Path) tmpDir + "/result";

        else if (*arg == "--attr" || *arg == "-A")
            attrPaths.push_back(getArg(*arg, arg, end));

        else if (*arg == "--drv-link")
            getArg(*arg, arg, end); // obsolete

        else if (*arg == "--out-link" || *arg == "-o")
            outLink = getArg(*arg, arg, end);

        else if (*arg == "--add-root")
            gcRoot = getArg(*arg, arg, end);

        else if (*arg == "--dry-run")
            dryRun = true;

        else if (*arg == "--repair") {
            repair = Repair;
            buildMode = bmRepair;
        }

        else if (*arg == "--run-env") // obsolete
            runEnv = true;

        else if (*arg == "--command" || *arg == "--run") {
            if (*arg == "--run")
                interactive = false;
            envCommand = getArg(*arg, arg, end) + "\nexit";
        }

        else if (*arg == "--check")
            buildMode = bmCheck;

        else if (*arg == "--exclude")
            envExclude.push_back(getArg(*arg, arg, end));

        else if (*arg == "--expr" || *arg == "-E")
            fromArgs = true;

        else if (*arg == "--pure") pure = true;
        else if (*arg == "--impure") pure = false;

        else if (*arg == "--packages" || *arg == "-p")
            packages = true;

        else if (inShebang && *arg == "-i") {
            auto interpreter = getArg(*arg, arg, end);
            interactive = false;
            auto execArgs = "";

            // Überhack to support Perl. Perl examines the shebang and
            // executes it unless it contains the string "perl" or "indir",
            // or (undocumented) argv[0] does not contain "perl". Exploit
            // the latter by doing "exec -a".
            if (std::regex_search(interpreter, std::regex("perl")))
                execArgs = "-a PERL";

            std::ostringstream joined;
            for (const auto & i : savedArgs)
                joined << shellEscape(i) << ' ';

            if (std::regex_search(interpreter, std::regex("ruby"))) {
                // Hack for Ruby. Ruby also examines the shebang. It tries to
                // read the shebang to understand which packages to read from. Since
                // this is handled via nix-shell -p, we wrap our ruby script execution
                // in ruby -e 'load' which ignores the shebangs.
                envCommand = (format("exec %1% %2% -e 'load(\"%3%\")' -- %4%") % execArgs % interpreter % script % joined.str()).str();
            } else {
                envCommand = (format("exec %1% %2% %3% %4%") % execArgs % interpreter % script % joined.str()).str();
            }
        }

        else if (*arg == "--keep")
            keepVars.insert(getArg(*arg, arg, end));

        else if (*arg == "-")
            readStdin = true;

        else if (*arg != "" && arg->at(0) == '-')
            return false;

        else
            left.push_back(*arg);

        return true;
    });

    myArgs.parseCmdline(args);

    initPlugins();

    if (packages && fromArgs)
        throw UsageError("'-p' and '-E' are mutually exclusive");

    auto store = openStore();

    auto state = std::make_unique<EvalState>(myArgs.searchPath, store);
    state->repair = repair;

    Bindings & autoArgs = *myArgs.getAutoArgs(*state);

    if (packages) {
        std::ostringstream joined;
        joined << "with import <nixpkgs> { }; (pkgs.runCommandCC or pkgs.runCommand) \"shell\" { buildInputs = [ ";
        for (const auto & i : left)
            joined << '(' << i << ") ";
        joined << "]; } \"\"";
        fromArgs = true;
        left = {joined.str()};
    } else if (!fromArgs) {
        if (left.empty() && runEnv && pathExists("shell.nix"))
            left = {"shell.nix"};
        if (left.empty())
            left = {"default.nix"};
    }

    if (runEnv)
        setenv("IN_NIX_SHELL", pure ? "pure" : "impure", 1);

    DrvInfos drvs;

    /* Parse the expressions. */
    std::vector<Expr *> exprs;

    if (readStdin)
        exprs = {state->parseStdin()};
    else
        for (auto i : left) {
            if (fromArgs)
                exprs.push_back(state->parseExprFromString(i, absPath(".")));
            else {
                auto absolute = i;
                try {
                    absolute = canonPath(absPath(i), true);
                } catch (Error & e) {};
                if (store->isStorePath(absolute) && std::regex_match(absolute, std::regex(".*\\.drv(!.*)?")))
                drvs.push_back(DrvInfo(*state, store, absolute));
            else
                /* If we're in a #! script, interpret filenames
                   relative to the script. */
                exprs.push_back(state->parseExprFromFile(resolveExprPath(state->checkSourcePath(lookupFileArg(*state,
                    inShebang && !packages ? absPath(i, absPath(dirOf(script))) : i)))));
            }
        }

    /* Evaluate them into derivations. */
    if (attrPaths.empty()) attrPaths = {""};

    for (auto e : exprs) {
        Value vRoot;
        state->eval(e, vRoot);

        for (auto & i : attrPaths) {
            Value & v(*findAlongAttrPath(*state, i, autoArgs, vRoot));
            state->forceValue(v);
            getDerivations(*state, v, "", autoArgs, drvs, false);
        }
    }

    state->printStats();

    auto buildPaths = [&](const PathSet & paths) {
        /* Note: we do this even when !printMissing to efficiently
           fetch binary cache data. */
        unsigned long long downloadSize, narSize;
        PathSet willBuild, willSubstitute, unknown;
        store->queryMissing(paths,
            willBuild, willSubstitute, unknown, downloadSize, narSize);

        if (settings.printMissing)
            printMissing(ref<Store>(store), willBuild, willSubstitute, unknown, downloadSize, narSize);

        if (!dryRun)
            store->buildPaths(paths, buildMode);
    };

    if (runEnv) {
        if (drvs.size() != 1)
            throw UsageError("nix-shell requires a single derivation");

        auto & drvInfo = drvs.front();
        auto drv = store->derivationFromPath(drvInfo.queryDrvPath());

        PathSet pathsToBuild;

        /* Figure out what bash shell to use. If $NIX_BUILD_SHELL
           is not set, then build bashInteractive from
           <nixpkgs>. */
        auto shell = getEnv("NIX_BUILD_SHELL", "");

        if (shell == "") {

            try {
                auto expr = state->parseExprFromString("(import <nixpkgs> {}).bashInteractive", absPath("."));

                Value v;
                state->eval(expr, v);

                auto drv = getDerivation(*state, v, false);
                if (!drv)
                    throw Error("the 'bashInteractive' attribute in <nixpkgs> did not evaluate to a derivation");

                pathsToBuild.insert(drv->queryDrvPath());

                shell = drv->queryOutPath() + "/bin/bash";

            } catch (Error & e) {
                printError("warning: %s; will use bash from your environment", e.what());
                shell = "bash";
            }
        }

        // Build or fetch all dependencies of the derivation.
        for (const auto & input : drv.inputDrvs)
            if (std::all_of(envExclude.cbegin(), envExclude.cend(), [&](const string & exclude) { return !std::regex_search(input.first, std::regex(exclude)); }))
                pathsToBuild.insert(makeDrvPathWithOutputs(input.first, input.second));
        for (const auto & src : drv.inputSrcs)
            pathsToBuild.insert(src);

        buildPaths(pathsToBuild);

        if (dryRun) return;

        // Set the environment.
        auto env = getEnv();

        auto tmp = getEnv("TMPDIR", getEnv("XDG_RUNTIME_DIR", "/tmp"));

        if (pure) {
            decltype(env) newEnv;
            for (auto & i : env)
                if (keepVars.count(i.first))
                    newEnv.emplace(i);
            env = newEnv;
            // NixOS hack: prevent /etc/bashrc from sourcing /etc/profile.
            env["__ETC_PROFILE_SOURCED"] = "1";
        }

        env["NIX_BUILD_TOP"] = env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmp;
        env["NIX_STORE"] = store->storeDir;
        env["NIX_BUILD_CORES"] = std::to_string(settings.buildCores);

        auto passAsFile = tokenizeString<StringSet>(get(drv.env, "passAsFile", ""));

        bool keepTmp = false;
        int fileNr = 0;

        for (auto & var : drv.env)
            if (passAsFile.count(var.first)) {
                keepTmp = true;
                string fn = ".attr-" + std::to_string(fileNr++);
                Path p = (Path) tmpDir + "/" + fn;
                writeFile(p, var.second);
                env[var.first + "Path"] = p;
            } else
                env[var.first] = var.second;

        restoreAffinity();

        /* Run a shell using the derivation's environment.  For
           convenience, source $stdenv/setup to setup additional
           environment variables and shell functions.  Also don't
           lose the current $PATH directories. */
        auto rcfile = (Path) tmpDir + "/rc";
        writeFile(rcfile, fmt(
                (keepTmp ? "" : "rm -rf '%1%'; "s) +
                "[ -n \"$PS1\" ] && [ -e ~/.bashrc ] && source ~/.bashrc; "
                "%2%"
                "dontAddDisableDepTrack=1; "
                "[ -e $stdenv/setup ] && source $stdenv/setup; "
                "%3%"
                "PATH=\"%4%:$PATH\"; "
                "SHELL=%5%; "
                "set +e; "
                R"s([ -n "$PS1" ] && PS1='\n\[\033[1;32m\][nix-shell:\w]\$\[\033[0m\] '; )s"
                "if [ \"$(type -t runHook)\" = function ]; then runHook shellHook; fi; "
                "unset NIX_ENFORCE_PURITY; "
                "shopt -u nullglob; "
                "unset TZ; %6%"
                "%7%",
                (Path) tmpDir,
                (pure ? "" : "p=$PATH; "),
                (pure ? "" : "PATH=$PATH:$p; unset p; "),
                dirOf(shell),
                shell,
                (getenv("TZ") ? (string("export TZ='") + getenv("TZ") + "'; ") : ""),
                envCommand));

        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(i.first + "=" + i.second);

        auto args = interactive
            ? Strings{"bash", "--rcfile", rcfile}
            : Strings{"bash", rcfile};

        auto envPtrs = stringsToCharPtrs(envStrs);

        environ = envPtrs.data();

        auto argPtrs = stringsToCharPtrs(args);

        restoreSignals();

        execvp(shell.c_str(), argPtrs.data());

        throw SysError("executing shell '%s'", shell);
    }

    else {

        PathSet pathsToBuild;

        std::map<Path, Path> drvPrefixes;
        std::map<Path, Path> resultSymlinks;
        std::vector<Path> outPaths;

        for (auto & drvInfo : drvs) {
            auto drvPath = drvInfo.queryDrvPath();
            auto outPath = drvInfo.queryOutPath();

            auto outputName = drvInfo.queryOutputName();
            if (outputName == "")
                throw Error("derivation '%s' lacks an 'outputName' attribute", drvPath);

            pathsToBuild.insert(drvPath + "!" + outputName);

            std::string drvPrefix;
            auto i = drvPrefixes.find(drvPath);
            if (i != drvPrefixes.end())
                drvPrefix = i->second;
            else {
                drvPrefix = outLink;
                if (drvPrefixes.size())
                    drvPrefix += fmt("-%d", drvPrefixes.size() + 1);
                drvPrefixes[drvPath] = drvPrefix;
            }

            std::string symlink = drvPrefix;
            if (outputName != "out") symlink += "-" + outputName;

            resultSymlinks[symlink] = outPath;
            outPaths.push_back(outPath);
        }

        buildPaths(pathsToBuild);

        if (dryRun) return;

        for (auto & symlink : resultSymlinks)
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                store2->addPermRoot(symlink.second, absPath(symlink.first), true);

        for (auto & path : outPaths)
            std::cout << path << '\n';
    }
}

static RegisterLegacyCommand s1("nix-build", _main);
static RegisterLegacyCommand s2("nix-shell", _main);

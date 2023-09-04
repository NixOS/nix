#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <sstream>
#include <vector>
#include <map>

#include <nlohmann/json.hpp>

#include "parsed-derivations.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "globals.hh"
#include "derivations.hh"
#include "util.hh"
#include "shared.hh"
#include "path-with-outputs.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "get-drvs.hh"
#include "common-eval-args.hh"
#include "attr-path.hh"
#include "legacy.hh"

using namespace nix;
using namespace std::string_literals;

extern char * * environ __attribute__((weak));

/* Recreate the effect of the perl shellwords function, breaking up a
 * string into arguments like a shell word, including escapes
 */
static std::vector<std::string> shellwords(const std::string & s)
{
    std::regex whitespace("^(\\s+).*");
    auto begin = s.cbegin();
    std::vector<std::string> res;
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

static void main_nix_build(int argc, char * * argv)
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
    std::vector<std::string> savedArgs;

    AutoDelete tmpDir(createTempDir("", myName));

    std::string outLink = "./result";

    // List of environment variables kept for --pure
    std::set<std::string> keepVars{
        "HOME", "XDG_RUNTIME_DIR", "USER", "LOGNAME", "DISPLAY",
        "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "PATH", "TERM", "IN_NIX_SHELL",
        "NIX_SHELL_PRESERVE_PROMPT", "TZ", "PAGER", "NIX_BUILD_SHELL", "SHLVL",
        "http_proxy", "https_proxy", "ftp_proxy", "all_proxy", "no_proxy"
    };

    Strings args;
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);

    // Heuristic to see if we're invoked as a shebang script, namely,
    // if we have at least one argument, it's the name of an
    // executable file, and it starts with "#!".
    if (runEnv && argc > 1) {
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

        else if (runEnv && (*arg == "--command" || *arg == "--run")) {
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

        else if (runEnv && (*arg == "--packages" || *arg == "-p"))
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
                envCommand = (format("exec %1% %2% -e 'load(ARGV.shift)' -- %3% %4%") % execArgs % interpreter % shellEscape(script) % joined.str()).str();
            } else {
                envCommand = (format("exec %1% %2% %3% %4%") % execArgs % interpreter % shellEscape(script) % joined.str()).str();
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

    if (packages && fromArgs)
        throw UsageError("'-p' and '-E' are mutually exclusive");

    auto store = openStore();
    auto evalStore = myArgs.evalStoreUrl ? openStore(*myArgs.evalStoreUrl) : store;

    auto state = std::make_unique<EvalState>(myArgs.searchPath, evalStore, store);
    state->repair = repair;

    auto autoArgs = myArgs.getAutoArgs(*state);

    if (runEnv) {
        auto newArgs = state->buildBindings(autoArgs->size() + 1);
        newArgs.alloc("inNixShell").mkBool(true);
        for (auto & i : *autoArgs) newArgs.insert(i);
        autoArgs = newArgs.finish();
    }

    if (packages) {
        std::ostringstream joined;
        joined << "{...}@args: with import <nixpkgs> args; (pkgs.runCommandCC or pkgs.runCommand) \"shell\" { buildInputs = [ ";
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
                exprs.push_back(state->parseExprFromString(std::move(i), absPath(".")));
            else {
                auto absolute = i;
                try {
                    absolute = canonPath(absPath(i), true);
                } catch (Error & e) {};
                auto [path, outputNames] = parsePathWithOutputs(absolute);
                if (evalStore->isStorePath(path) && hasSuffix(path, ".drv"))
                    drvs.push_back(DrvInfo(*state, evalStore, absolute));
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
            Value & v(*findAlongAttrPath(*state, i, *autoArgs, vRoot).first);
            state->forceValue(v, [&]() { return v.determinePos(noPos); });
            getDerivations(*state, v, "", *autoArgs, drvs, false);
        }
    }

    state->printStats();

    auto buildPaths = [&](const std::vector<DerivedPath> & paths) {
        /* Note: we do this even when !printMissing to efficiently
           fetch binary cache data. */
        uint64_t downloadSize, narSize;
        StorePathSet willBuild, willSubstitute, unknown;
        store->queryMissing(paths,
            willBuild, willSubstitute, unknown, downloadSize, narSize);

        if (settings.printMissing)
            printMissing(ref<Store>(store), willBuild, willSubstitute, unknown, downloadSize, narSize);

        if (!dryRun)
            store->buildPaths(paths, buildMode, evalStore);
    };

    if (runEnv) {
        if (drvs.size() != 1)
            throw UsageError("nix-shell requires a single derivation");

        auto & drvInfo = drvs.front();
        auto drv = evalStore->derivationFromPath(drvInfo.requireDrvPath());

        std::vector<DerivedPath> pathsToBuild;
        RealisedPath::Set pathsToCopy;

        /* Figure out what bash shell to use. If $NIX_BUILD_SHELL
           is not set, then build bashInteractive from
           <nixpkgs>. */
        auto shell = getEnv("NIX_BUILD_SHELL");
        std::optional<StorePath> shellDrv;

        if (!shell) {

            try {
                auto expr = state->parseExprFromString("(import <nixpkgs> {}).bashInteractive", absPath("."));

                Value v;
                state->eval(expr, v);

                auto drv = getDerivation(*state, v, false);
                if (!drv)
                    throw Error("the 'bashInteractive' attribute in <nixpkgs> did not evaluate to a derivation");

                auto bashDrv = drv->requireDrvPath();
                pathsToBuild.push_back(DerivedPath::Built {
                    .drvPath = bashDrv,
                    .outputs = {},
                });
                pathsToCopy.insert(bashDrv);
                shellDrv = bashDrv;

            } catch (Error & e) {
                logError(e.info());
                notice("will use bash from your environment");
                shell = "bash";
            }
        }

        // Build or fetch all dependencies of the derivation.
        for (const auto & [inputDrv0, inputOutputs] : drv.inputDrvs) {
            // To get around lambda capturing restrictions in the
            // standard.
            const auto & inputDrv = inputDrv0;
            if (std::all_of(envExclude.cbegin(), envExclude.cend(),
                    [&](const std::string & exclude) {
                        return !std::regex_search(store->printStorePath(inputDrv), std::regex(exclude));
                    }))
            {
                pathsToBuild.push_back(DerivedPath::Built {
                    .drvPath = inputDrv,
                    .outputs = inputOutputs
                });
                pathsToCopy.insert(inputDrv);
            }
        }
        for (const auto & src : drv.inputSrcs) {
            pathsToBuild.push_back(DerivedPath::Opaque{src});
            pathsToCopy.insert(src);
        }

        buildPaths(pathsToBuild);

        if (dryRun) return;

        if (shellDrv) {
            auto shellDrvOutputs = store->queryPartialDerivationOutputMap(shellDrv.value());
            shell = store->printStorePath(shellDrvOutputs.at("out").value()) + "/bin/bash";
        }

        if (settings.isExperimentalFeatureEnabled(Xp::CaDerivations)) {
            auto resolvedDrv = drv.tryResolve(*store);
            assert(resolvedDrv && "Successfully resolved the derivation");
            drv = *resolvedDrv;
        }

        // Set the environment.
        auto env = getEnv();

        auto tmp = getEnv("TMPDIR");
        if (!tmp) tmp = getEnv("XDG_RUNTIME_DIR").value_or("/tmp");

        if (pure) {
            decltype(env) newEnv;
            for (auto & i : env)
                if (keepVars.count(i.first))
                    newEnv.emplace(i);
            env = newEnv;
            // NixOS hack: prevent /etc/bashrc from sourcing /etc/profile.
            env["__ETC_PROFILE_SOURCED"] = "1";
        }

        env["NIX_BUILD_TOP"] = env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = *tmp;
        env["NIX_STORE"] = store->storeDir;
        env["NIX_BUILD_CORES"] = std::to_string(settings.buildCores);

        auto passAsFile = tokenizeString<StringSet>(get(drv.env, "passAsFile").value_or(""));

        bool keepTmp = false;
        int fileNr = 0;

        for (auto & var : drv.env)
            if (passAsFile.count(var.first)) {
                keepTmp = true;
                auto fn = ".attr-" + std::to_string(fileNr++);
                Path p = (Path) tmpDir + "/" + fn;
                writeFile(p, var.second);
                env[var.first + "Path"] = p;
            } else
                env[var.first] = var.second;

        std::string structuredAttrsRC;

        if (env.count("__json")) {
            StorePathSet inputs;
            for (auto & [depDrvPath, wantedDepOutputs] : drv.inputDrvs) {
                auto outputs = evalStore->queryPartialDerivationOutputMap(depDrvPath);
                for (auto & i : wantedDepOutputs) {
                    auto o = outputs.at(i);
                    store->computeFSClosure(*o, inputs);
                }
            }

            ParsedDerivation parsedDrv(drvInfo.requireDrvPath(), drv);

            if (auto structAttrs = parsedDrv.prepareStructuredAttrs(*store, inputs)) {
                auto json = structAttrs.value();
                structuredAttrsRC = writeStructuredAttrsShell(json);

                auto attrsJSON = (Path) tmpDir + "/.attrs.json";
                writeFile(attrsJSON, json.dump());

                auto attrsSH = (Path) tmpDir + "/.attrs.sh";
                writeFile(attrsSH, structuredAttrsRC);

                env["NIX_ATTRS_SH_FILE"] = attrsSH;
                env["NIX_ATTRS_JSON_FILE"] = attrsJSON;
                keepTmp = true;
            }
        }

        /* Run a shell using the derivation's environment.  For
           convenience, source $stdenv/setup to setup additional
           environment variables and shell functions.  Also don't
           lose the current $PATH directories. */
        auto rcfile = (Path) tmpDir + "/rc";
        std::string rc = fmt(
                R"(_nix_shell_clean_tmpdir() { command rm -rf %1%; }; )"s +
                (keepTmp ?
                    "trap _nix_shell_clean_tmpdir EXIT; "
                    "exitHooks+=(_nix_shell_clean_tmpdir); "
                    "failureHooks+=(_nix_shell_clean_tmpdir); ":
                    "_nix_shell_clean_tmpdir; ") +
                (pure ? "" : "[ -n \"$PS1\" ] && [ -e ~/.bashrc ] && source ~/.bashrc;") +
                "%2%"
                // always clear PATH.
                // when nix-shell is run impure, we rehydrate it with the `p=$PATH` above
                "unset PATH;"
                "dontAddDisableDepTrack=1;\n"
                + structuredAttrsRC +
                "\n[ -e $stdenv/setup ] && source $stdenv/setup; "
                "%3%"
                "PATH=%4%:\"$PATH\"; "
                "SHELL=%5%; "
                "BASH=%5%; "
                "set +e; "
                R"s([ -n "$PS1" -a -z "$NIX_SHELL_PRESERVE_PROMPT" ] && PS1='\n\[\033[1;32m\][nix-shell:\w]\$\[\033[0m\] '; )s"
                "if [ \"$(type -t runHook)\" = function ]; then runHook shellHook; fi; "
                "unset NIX_ENFORCE_PURITY; "
                "shopt -u nullglob; "
                "unset TZ; %6%"
                "shopt -s execfail;"
                "%7%",
                shellEscape(tmpDir),
                (pure ? "" : "p=$PATH; "),
                (pure ? "" : "PATH=$PATH:$p; unset p; "),
                shellEscape(dirOf(*shell)),
                shellEscape(*shell),
                (getenv("TZ") ? (std::string("export TZ=") + shellEscape(getenv("TZ")) + "; ") : ""),
                envCommand);
        vomit("Sourcing nix-shell with file %s and contents:\n%s", rcfile, rc);
        writeFile(rcfile, rc);

        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(i.first + "=" + i.second);

        auto args = interactive
            ? Strings{"bash", "--rcfile", rcfile}
            : Strings{"bash", rcfile};

        auto envPtrs = stringsToCharPtrs(envStrs);

        environ = envPtrs.data();

        auto argPtrs = stringsToCharPtrs(args);

        restoreProcessContext();

        logger->stop();

        execvp(shell->c_str(), argPtrs.data());

        throw SysError("executing shell '%s'", *shell);
    }

    else {

        std::vector<DerivedPath> pathsToBuild;
        std::vector<std::pair<StorePath, std::string>> pathsToBuildOrdered;
        RealisedPath::Set drvsToCopy;

        std::map<StorePath, std::pair<size_t, StringSet>> drvMap;

        for (auto & drvInfo : drvs) {
            auto drvPath = drvInfo.requireDrvPath();

            auto outputName = drvInfo.queryOutputName();
            if (outputName == "")
                throw Error("derivation '%s' lacks an 'outputName' attribute", store->printStorePath(drvPath));

            pathsToBuild.push_back(DerivedPath::Built{drvPath, {outputName}});
            pathsToBuildOrdered.push_back({drvPath, {outputName}});
            drvsToCopy.insert(drvPath);

            auto i = drvMap.find(drvPath);
            if (i != drvMap.end())
                i->second.second.insert(outputName);
            else
                drvMap[drvPath] = {drvMap.size(), {outputName}};
        }

        buildPaths(pathsToBuild);

        if (dryRun) return;

        std::vector<StorePath> outPaths;

        for (auto & [drvPath, outputName] : pathsToBuildOrdered) {
            auto & [counter, _wantedOutputs] = drvMap.at({drvPath});
            std::string drvPrefix = outLink;
            if (counter)
                drvPrefix += fmt("-%d", counter + 1);

            auto builtOutputs = evalStore->queryPartialDerivationOutputMap(drvPath);

            auto maybeOutputPath = builtOutputs.at(outputName);
            assert(maybeOutputPath);
            auto outputPath = *maybeOutputPath;

            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>()) {
                std::string symlink = drvPrefix;
                if (outputName != "out") symlink += "-" + outputName;
                store2->addPermRoot(outputPath, absPath(symlink));
            }

            outPaths.push_back(outputPath);
        }

        logger->stop();

        for (auto & path : outPaths)
            std::cout << store->printStorePath(path) << '\n';
    }
}

static RegisterLegacyCommand r_nix_build("nix-build", main_nix_build);
static RegisterLegacyCommand r_nix_shell("nix-shell", main_nix_build);

#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <sstream>
#include <vector>
#include <map>

#include <nlohmann/json.hpp>

#include "nix/util/current-process.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/store-open.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/realisation.hh"
#include "nix/store/derivations.hh"
#include "nix/main/shared.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/cmd/common-eval-args.hh"
#include "nix/expr/attr-path.hh"
#include "nix/cmd/legacy.hh"
#include "nix/util/users.hh"
#include "nix/cmd/network-proxy.hh"
#include "nix/cmd/compatibility-settings.hh"
#include "man-pages.hh"

using namespace nix;
using namespace std::string_literals;

extern char ** environ __attribute__((weak));

/* Recreate the effect of the perl shellwords function, breaking up a
 * string into arguments like a shell word, including escapes
 */
static std::vector<std::string> shellwords(std::string_view s)
{
    std::regex whitespace("^\\s+");
    auto begin = s.cbegin();
    std::vector<std::string> res;
    std::string cur;

    enum state { sBegin, sSingleQuote, sDoubleQuote };

    state st = sBegin;
    auto it = begin;
    for (; it != s.cend(); ++it) {
        if (st == sBegin) {
            std::cmatch match;
            if (regex_search(it, s.cend(), match, whitespace)) {
                cur.append(begin, it);
                res.push_back(cur);
                it = match[0].second;
                if (it == s.cend())
                    return res;
                begin = it;
                cur.clear();
            }
        }
        switch (*it) {
        case '\'':
            if (st != sDoubleQuote) {
                cur.append(begin, it);
                begin = it + 1;
                st = st == sBegin ? sSingleQuote : sBegin;
            }
            break;
        case '"':
            if (st != sSingleQuote) {
                cur.append(begin, it);
                begin = it + 1;
                st = st == sBegin ? sDoubleQuote : sBegin;
            }
            break;
        case '\\':
            if (st != sSingleQuote) {
                /* perl shellwords mostly just treats the next char as part of the string with no special processing */
                cur.append(begin, it);
                begin = ++it;
            }
            break;
        }
    }
    if (st != sBegin)
        throw Error("unterminated quote in shebang line");
    cur.append(begin, it);
    res.push_back(cur);
    return res;
}

/**
 * Like `resolveExprPath`, but prefers `shell.nix` instead of `default.nix`,
 * and if `path` was a directory, it checks eagerly whether `shell.nix` or
 * `default.nix` exist, throwing an error if they don't.
 */
static SourcePath resolveShellExprPath(SourcePath path)
{
    auto resolvedOrDir = resolveExprPath(path, false);
    if (resolvedOrDir.resolveSymlinks().lstat().type == SourceAccessor::tDirectory) {
        if ((resolvedOrDir / "shell.nix").pathExists()) {
            if (compatibilitySettings.nixShellAlwaysLooksForShellNix) {
                return resolvedOrDir / "shell.nix";
            } else {
                warn(
                    "Skipping '%1%', because the setting '%2%' is disabled. This is a deprecated behavior. Consider enabling '%2%'.",
                    resolvedOrDir / "shell.nix",
                    "nix-shell-always-looks-for-shell-nix");
            }
        }
        if ((resolvedOrDir / "default.nix").pathExists()) {
            return resolvedOrDir / "default.nix";
        }
        throw Error("neither '%s' nor '%s' found in '%s'", "shell.nix", "default.nix", resolvedOrDir);
    }
    return resolvedOrDir;
}

static void main_nix_build(int argc, char ** argv)
{
    auto dryRun = false;
    auto isNixShell = std::regex_search(argv[0], std::regex("nix-shell$"));
    auto pure = false;
    auto fromArgs = false;
    auto packages = false;
    // Same condition as bash uses for interactive shells
    auto interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
    Strings attrPaths;
    Strings remainingArgs;
    BuildMode buildMode = bmNormal;
    bool readStdin = false;

    std::string envCommand; // interactive shell
    Strings envExclude;

    auto myName = isNixShell ? "nix-shell" : "nix-build";

    auto inShebang = false;
    std::string script;
    std::vector<std::string> savedArgs;

    AutoDelete tmpDir(createTempDir("", myName));

    std::string outLink = "./result";

    // List of environment variables kept for --pure
    StringSet keepVars{
        "HOME",
        "XDG_RUNTIME_DIR",
        "USER",
        "LOGNAME",
        "DISPLAY",
        "WAYLAND_DISPLAY",
        "WAYLAND_SOCKET",
        "PATH",
        "TERM",
        "IN_NIX_SHELL",
        "NIX_SHELL_PRESERVE_PROMPT",
        "TZ",
        "PAGER",
        "NIX_BUILD_SHELL",
        "SHLVL",
    };
    keepVars.insert(networkProxyVariables.begin(), networkProxyVariables.end());

    Strings args;
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);

    // Heuristic to see if we're invoked as a shebang script, namely,
    // if we have at least one argument, it's the name of an
    // executable file, and it starts with "#!".
    if (isNixShell && argc > 1) {
        script = argv[1];
        try {
            auto lines = tokenizeString<Strings>(readFile(script), "\n");
            if (!lines.empty() && std::regex_search(lines.front(), std::regex("^#!"))) {
                lines.pop_front();
                inShebang = true;
                for (int i = 2; i < argc; ++i)
                    savedArgs.push_back(argv[i]);
                args.clear();
                for (auto line : lines) {
                    line = chomp(line);
                    std::smatch match;
                    if (std::regex_match(line, match, std::regex("^#!\\s*nix-shell\\s+(.*)$")))
                        for (const auto & word : shellwords({match[1].first, match[1].second}))
                            args.push_back(word);
                }
            }
        } catch (SystemError &) {
        }
    }

    struct MyArgs : LegacyArgs, MixEvalArgs
    {
        using LegacyArgs::LegacyArgs;

        void setBaseDir(Path baseDir)
        {
            commandBaseDir = baseDir;
        }
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
            outLink = (tmpDir.path() / "result").string();

        else if (*arg == "--attr" || *arg == "-A")
            attrPaths.push_back(getArg(*arg, arg, end));

        else if (*arg == "--drv-link")
            getArg(*arg, arg, end); // obsolete

        else if (*arg == "--out-link" || *arg == "-o")
            outLink = getArg(*arg, arg, end);

        else if (*arg == "--dry-run")
            dryRun = true;

        else if (*arg == "--run-env") // obsolete
            isNixShell = true;

        else if (isNixShell && (*arg == "--command" || *arg == "--run")) {
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

        else if (*arg == "--pure")
            pure = true;
        else if (*arg == "--impure")
            pure = false;

        else if (isNixShell && (*arg == "--packages" || *arg == "-p"))
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
                joined << escapeShellArgAlways(i) << ' ';

            if (std::regex_search(interpreter, std::regex("ruby"))) {
                // Hack for Ruby. Ruby also examines the shebang. It tries to
                // read the shebang to understand which packages to read from. Since
                // this is handled via nix-shell -p, we wrap our ruby script execution
                // in ruby -e 'load' which ignores the shebangs.
                envCommand =
                    fmt("exec %1% %2% -e 'load(ARGV.shift)' -- %3% %4%",
                        execArgs,
                        interpreter,
                        escapeShellArgAlways(script),
                        joined.view());
            } else {
                envCommand =
                    fmt("exec %1% %2% %3% %4%", execArgs, interpreter, escapeShellArgAlways(script), joined.view());
            }
        }

        else if (*arg == "--keep")
            keepVars.insert(getArg(*arg, arg, end));

        else if (*arg == "-")
            readStdin = true;

        else if (*arg != "" && arg->at(0) == '-')
            return false;

        else
            remainingArgs.push_back(*arg);

        return true;
    });

    myArgs.parseCmdline(args);

    if (packages && fromArgs)
        throw UsageError("'-p' and '-E' are mutually exclusive");

    auto store = openStore();
    auto evalStore = myArgs.evalStoreUrl ? openStore(*myArgs.evalStoreUrl) : store;

    auto state = std::make_unique<EvalState>(myArgs.lookupPath, evalStore, fetchSettings, evalSettings, store);
    state->repair = myArgs.repair;
    if (myArgs.repair)
        buildMode = bmRepair;

    if (inShebang && compatibilitySettings.nixShellShebangArgumentsRelativeToScript) {
        myArgs.setBaseDir(absPath(dirOf(script)));
    }
    auto autoArgs = myArgs.getAutoArgs(*state);

    auto autoArgsWithInNixShell = autoArgs;
    if (isNixShell) {
        auto newArgs = state->buildBindings(autoArgsWithInNixShell->size() + 1);
        newArgs.alloc("inNixShell").mkBool(true);
        for (auto & i : *autoArgs)
            newArgs.insert(i);
        autoArgsWithInNixShell = newArgs.finish();
    }

    if (packages) {
        std::ostringstream joined;
        joined
            << "{...}@args: with import <nixpkgs> args; (pkgs.runCommandCC or pkgs.runCommand) \"shell\" { buildInputs = [ ";
        for (const auto & i : remainingArgs)
            joined << '(' << i << ") ";
        joined << "]; } \"\"";
        fromArgs = true;
        remainingArgs = {joined.str()};
    } else if (!fromArgs && remainingArgs.empty()) {
        if (isNixShell && !compatibilitySettings.nixShellAlwaysLooksForShellNix
            && std::filesystem::exists("shell.nix")) {
            // If we're in 2.3 compatibility mode, we need to look for shell.nix
            // now, because it won't be done later.
            remainingArgs = {"shell.nix"};
        } else {
            remainingArgs = {"."};

            // Instead of letting it throw later, we throw here to give a more relevant error message
            if (isNixShell && !std::filesystem::exists("shell.nix") && !std::filesystem::exists("default.nix"))
                throw Error(
                    "no argument specified and no '%s' or '%s' file found in the working directory",
                    "shell.nix",
                    "default.nix");
        }
    }

    if (isNixShell)
        setEnv("IN_NIX_SHELL", pure ? "pure" : "impure");

    PackageInfos drvs;

    /* Parse the expressions. */
    std::vector<Expr *> exprs;

    if (readStdin)
        exprs = {state->parseStdin()};
    else
        for (auto i : remainingArgs) {
            if (fromArgs) {
                auto shebangBaseDir = absPath(dirOf(script));
                exprs.push_back(state->parseExprFromString(
                    std::move(i),
                    (inShebang && compatibilitySettings.nixShellShebangArgumentsRelativeToScript)
                        ? lookupFileArg(*state, shebangBaseDir)
                        : state->rootPath(".")));
            } else {
                auto absolute = i;
                try {
                    absolute = canonPath(absPath(i), true);
                } catch (Error & e) {
                };
                auto [path, outputNames] = parsePathWithOutputs(absolute);
                if (evalStore->isStorePath(path) && hasSuffix(path, ".drv"))
                    drvs.push_back(PackageInfo(*state, evalStore, absolute));
                else {
                    /* If we're in a #! script, interpret filenames
                       relative to the script. */
                    auto baseDir = inShebang && !packages ? absPath(i, absPath(dirOf(script))) : i;

                    auto sourcePath = lookupFileArg(*state, baseDir);
                    auto resolvedPath = isNixShell ? resolveShellExprPath(sourcePath) : resolveExprPath(sourcePath);

                    exprs.push_back(state->parseExprFromFile(resolvedPath));
                }
            }
        }

    /* Evaluate them into derivations. */
    if (attrPaths.empty())
        attrPaths = {""};

    for (auto e : exprs) {
        Value vRoot;
        state->eval(e, vRoot);

        auto takesNixShellAttr = [&](const Value & v) {
            if (!isNixShell) {
                return false;
            }
            bool add = false;
            if (v.type() == nFunction) {
                if (auto formals = v.lambda().fun->getFormals()) {
                    for (auto & i : formals->formals) {
                        if (state->symbols[i.name] == "inNixShell") {
                            add = true;
                            break;
                        }
                    }
                }
            }
            return add;
        };

        for (auto & i : attrPaths) {
            Value & v(
                *findAlongAttrPath(*state, i, takesNixShellAttr(vRoot) ? *autoArgsWithInNixShell : *autoArgs, vRoot)
                     .first);
            state->forceValue(v, v.determinePos(noPos));
            getDerivations(*state, v, "", takesNixShellAttr(v) ? *autoArgsWithInNixShell : *autoArgs, drvs, false);
        }
    }

    state->maybePrintStats();

    auto buildPaths = [&](const std::vector<DerivedPath> & paths) {
        if (settings.printMissing)
            printMissing(ref<Store>(store), paths);

        if (!dryRun)
            store->buildPaths(paths, buildMode, evalStore);
    };

    if (isNixShell) {
        if (drvs.size() != 1)
            throw UsageError("nix-shell requires a single derivation");

        auto & packageInfo = drvs.front();
        auto drv = evalStore->derivationFromPath(packageInfo.requireDrvPath());

        std::vector<DerivedPath> pathsToBuild;
        RealisedPath::Set pathsToCopy;

        /* Figure out what bash shell to use. If $NIX_BUILD_SHELL
           is not set, then build bashInteractive from
           <nixpkgs>. */
        auto shell = getEnv("NIX_BUILD_SHELL");
        std::optional<StorePath> shellDrv;

        if (!shell) {

            try {
                auto expr = state->parseExprFromString("(import <nixpkgs> {}).bashInteractive", state->rootPath("."));

                Value v;
                state->eval(expr, v);

                auto drv = getDerivation(*state, v, false);
                if (!drv)
                    throw Error("the 'bashInteractive' attribute in <nixpkgs> did not evaluate to a derivation");

                auto bashDrv = drv->requireDrvPath();
                pathsToBuild.push_back(
                    DerivedPath::Built{
                        .drvPath = makeConstantStorePathRef(bashDrv),
                        .outputs = OutputsSpec::Names{"out"},
                    });
                pathsToCopy.insert(bashDrv);
                shellDrv = bashDrv;

            } catch (Error & e) {
                logError(e.info());
                notice("uses bash from your environment");
                shell = "bash";
            }
        }

        auto accumDerivedPath = [&](this auto & self,
                                    ref<SingleDerivedPath> inputDrv,
                                    const DerivedPathMap<StringSet>::ChildNode & inputNode) -> void {
            if (!inputNode.value.empty())
                pathsToBuild.push_back(
                    DerivedPath::Built{
                        .drvPath = inputDrv,
                        .outputs = OutputsSpec::Names{inputNode.value},
                    });
            for (const auto & [outputName, childNode] : inputNode.childMap)
                self(make_ref<SingleDerivedPath>(SingleDerivedPath::Built{inputDrv, outputName}), childNode);
        };

        // Build or fetch all dependencies of the derivation.
        for (const auto & [inputDrv0, inputNode] : drv.inputDrvs.map) {
            // To get around lambda capturing restrictions in the
            // standard.
            const auto & inputDrv = inputDrv0;
            if (std::all_of(envExclude.cbegin(), envExclude.cend(), [&](const std::string & exclude) {
                    return !std::regex_search(store->printStorePath(inputDrv), std::regex(exclude));
                })) {
                accumDerivedPath(makeConstantStorePathRef(inputDrv), inputNode);
                pathsToCopy.insert(inputDrv);
            }
        }
        for (const auto & src : drv.inputSrcs) {
            pathsToBuild.emplace_back(DerivedPath::Opaque{src});
            pathsToCopy.insert(src);
        }

        buildPaths(pathsToBuild);

        if (dryRun)
            return;

        if (shellDrv) {
            auto shellDrvOutputs = store->queryPartialDerivationOutputMap(shellDrv.value(), &*evalStore);
            shell = store->printStorePath(shellDrvOutputs.at("out").value()) + "/bin/bash";
        }

        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
            auto resolvedDrv = drv.tryResolve(*store);
            assert(resolvedDrv && "Successfully resolved the derivation");
            drv = *resolvedDrv;
        }

        // Set the environment.
        auto env = getEnv();

        if (pure) {
            decltype(env) newEnv;
            for (auto & i : env)
                if (keepVars.count(i.first))
                    newEnv.emplace(i);
            env = newEnv;
            // NixOS hack: prevent /etc/bashrc from sourcing /etc/profile.
            env["__ETC_PROFILE_SOURCED"] = "1";
        }

        env["NIX_BUILD_TOP"] = env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDir.path().string();
        env["NIX_STORE"] = store->storeDir;
        env["NIX_BUILD_CORES"] = fmt("%d", settings.buildCores ? settings.buildCores : settings.getDefaultCores());

        DerivationOptions drvOptions;
        try {
            drvOptions = DerivationOptions::fromStructuredAttrs(drv.env, drv.structuredAttrs);
        } catch (Error & e) {
            e.addTrace({}, "while parsing derivation '%s'", store->printStorePath(packageInfo.requireDrvPath()));
            throw;
        }

        int fileNr = 0;

        for (auto & var : drv.env)
            if (drvOptions.passAsFile.count(var.first)) {
                auto fn = ".attr-" + std::to_string(fileNr++);
                Path p = (tmpDir.path() / fn).string();
                writeFile(p, var.second);
                env[var.first + "Path"] = p;
            } else
                env[var.first] = var.second;

        std::string structuredAttrsRC;

        if (drv.structuredAttrs) {
            StorePathSet inputs;

            std::function<void(const StorePath &, const DerivedPathMap<StringSet>::ChildNode &)> accumInputClosure;

            accumInputClosure = [&](const StorePath & inputDrv,
                                    const DerivedPathMap<StringSet>::ChildNode & inputNode) {
                auto outputs = store->queryPartialDerivationOutputMap(inputDrv, &*evalStore);
                for (auto & i : inputNode.value) {
                    auto o = outputs.at(i);
                    store->computeFSClosure(*o, inputs);
                }
                for (const auto & [outputName, childNode] : inputNode.childMap)
                    accumInputClosure(*outputs.at(outputName), childNode);
            };

            for (const auto & [inputDrv, inputNode] : drv.inputDrvs.map)
                accumInputClosure(inputDrv, inputNode);

            auto json = drv.structuredAttrs->prepareStructuredAttrs(*store, drvOptions, inputs, drv.outputs);

            structuredAttrsRC = StructuredAttrs::writeShell(json);

            auto attrsJSON = (tmpDir.path() / ".attrs.json").string();
            writeFile(attrsJSON, static_cast<nlohmann::json>(std::move(json)).dump());

            auto attrsSH = (tmpDir.path() / ".attrs.sh").string();
            writeFile(attrsSH, structuredAttrsRC);

            env["NIX_ATTRS_SH_FILE"] = attrsSH;
            env["NIX_ATTRS_JSON_FILE"] = attrsJSON;
        }

        /* Run a shell using the derivation's environment.  For
           convenience, source $stdenv/setup to setup additional
           environment variables and shell functions.  Also don't
           lose the current $PATH directories. */
        auto rcfile = (tmpDir.path() / "rc").string();
        std::string rc = fmt(
                (R"(_nix_shell_clean_tmpdir() { command rm -rf %1%; };)"s
                  "trap _nix_shell_clean_tmpdir EXIT; "
                  "exitHooks+=(_nix_shell_clean_tmpdir); "
                  "failureHooks+=(_nix_shell_clean_tmpdir); ") +
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
                R"s([ -n "$PS1" -a -z "$NIX_SHELL_PRESERVE_PROMPT" ] && )s" +
                (isRootUser()
                    ? R"s(PS1='\n\[\033[1;31m\][nix-shell:\w]\$\[\033[0m\] '; )s"
                    : R"s(PS1='\n\[\033[1;32m\][nix-shell:\w]\$\[\033[0m\] '; )s") +
                "if [ \"$(type -t runHook)\" = function ]; then runHook shellHook; fi; "
                "unset NIX_ENFORCE_PURITY; "
                "shopt -u nullglob; "
                "unset TZ; %6%"
                "shopt -s execfail;"
                "%7%",
                escapeShellArgAlways(tmpDir.path().string()),
                (pure ? "" : "p=$PATH; "),
                (pure ? "" : "PATH=$PATH:$p; unset p; "),
                escapeShellArgAlways(dirOf(*shell)),
                escapeShellArgAlways(*shell),
                (getenv("TZ") ? (std::string("export TZ=") + escapeShellArgAlways(getenv("TZ")) + "; ") : ""),
                envCommand);
        vomit("Sourcing nix-shell with file %s and contents:\n%s", rcfile, rc);
        writeFile(rcfile, rc);

        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(i.first + "=" + i.second);

        auto args = interactive ? Strings{"bash", "--rcfile", rcfile} : Strings{"bash", rcfile};

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

        for (auto & packageInfo : drvs) {
            auto drvPath = packageInfo.requireDrvPath();

            auto outputName = packageInfo.queryOutputName();
            if (outputName == "")
                throw Error("derivation '%s' lacks an 'outputName' attribute", store->printStorePath(drvPath));

            pathsToBuild.push_back(
                DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(drvPath),
                    .outputs = OutputsSpec::Names{outputName},
                });
            pathsToBuildOrdered.push_back({drvPath, {outputName}});
            drvsToCopy.insert(drvPath);

            auto i = drvMap.find(drvPath);
            if (i != drvMap.end())
                i->second.second.insert(outputName);
            else
                drvMap[drvPath] = {drvMap.size(), {outputName}};
        }

        buildPaths(pathsToBuild);

        if (dryRun)
            return;

        std::vector<StorePath> outPaths;

        for (auto & [drvPath, outputName] : pathsToBuildOrdered) {
            auto & [counter, _wantedOutputs] = drvMap.at({drvPath});
            std::string drvPrefix = outLink;
            if (counter)
                drvPrefix += fmt("-%d", counter + 1);

            auto builtOutputs = store->queryPartialDerivationOutputMap(drvPath, &*evalStore);

            auto maybeOutputPath = builtOutputs.at(outputName);
            assert(maybeOutputPath);
            auto outputPath = *maybeOutputPath;

            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>()) {
                std::string symlink = drvPrefix;
                if (outputName != "out")
                    symlink += "-" + outputName;
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

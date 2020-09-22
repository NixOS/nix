#include "eval.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "progress-bar.hh"

#include <regex>

using namespace nix;

struct Var
{
    bool exported = true;
    bool associative = false;
    std::string quoted; // quoted string or array
};

struct BuildEnvironment
{
    std::map<std::string, Var> env;
    std::string bashFunctions;
};

BuildEnvironment readEnvironment(const Path & path)
{
    BuildEnvironment res;

    std::set<std::string> exported;

    debug("reading environment file '%s'", path);

    auto file = readFile(path);

    auto pos = file.cbegin();

    static std::string varNameRegex =
        R"re((?:[a-zA-Z_][a-zA-Z0-9_]*))re";

    static std::regex declareRegex(
        "^declare -x (" + varNameRegex + ")" +
        R"re((?:="((?:[^"\\]|\\.)*)")?\n)re");

    static std::string simpleStringRegex =
        R"re((?:[a-zA-Z0-9_/:\.\-\+=]*))re";

    static std::string quotedStringRegex =
        R"re((?:\$?'(?:[^'\\]|\\[abeEfnrtv\\'"?])*'))re";

    static std::string indexedArrayRegex =
        R"re((?:\(( *\[[0-9]+\]="(?:[^"\\]|\\.)*")*\)))re";

    static std::regex varRegex(
        "^(" + varNameRegex + ")=(" + simpleStringRegex + "|" + quotedStringRegex + "|" + indexedArrayRegex + ")\n");

    /* Note: we distinguish between an indexed and associative array
       using the space before the closing parenthesis. Will
       undoubtedly regret this some day. */
    static std::regex assocArrayRegex(
        "^(" + varNameRegex + ")=" + R"re((?:\(( *\[[^\]]+\]="(?:[^"\\]|\\.)*")* *\)))re" + "\n");

    static std::regex functionRegex(
        "^" + varNameRegex + " \\(\\) *\n");

    while (pos != file.end()) {

        std::smatch match;

        if (std::regex_search(pos, file.cend(), match, declareRegex, std::regex_constants::match_continuous)) {
            pos = match[0].second;
            exported.insert(match[1]);
        }

        else if (std::regex_search(pos, file.cend(), match, varRegex, std::regex_constants::match_continuous)) {
            pos = match[0].second;
            res.env.insert({match[1], Var { .exported = exported.count(match[1]) > 0, .quoted = match[2] }});
        }

        else if (std::regex_search(pos, file.cend(), match, assocArrayRegex, std::regex_constants::match_continuous)) {
            pos = match[0].second;
            res.env.insert({match[1], Var { .associative = true, .quoted = match[2] }});
        }

        else if (std::regex_search(pos, file.cend(), match, functionRegex, std::regex_constants::match_continuous)) {
            res.bashFunctions = std::string(pos, file.cend());
            break;
        }

        else throw Error("shell environment '%s' has unexpected line '%s'",
            path, file.substr(pos - file.cbegin(), 60));
    }

    res.env.erase("__output");

    return res;
}

const static std::string getEnvSh =
    #include "get-env.sh.gen.hh"
    ;

/* Given an existing derivation, return the shell environment as
   initialised by stdenv's setup script. We do this by building a
   modified derivation with the same dependencies and nearly the same
   initial environment variables, that just writes the resulting
   environment to a file and exits. */
StorePath getDerivationEnvironment(ref<Store> store, const StorePath & drvPath)
{
    auto drv = store->derivationFromPath(drvPath);

    auto builder = baseNameOf(drv.builder);
    if (builder != "bash")
        throw Error("'nix develop' only works on derivations that use 'bash' as their builder");

    auto getEnvShPath = store->addTextToStore("get-env.sh", getEnvSh, {});

    drv.args = {store->printStorePath(getEnvShPath)};

    /* Remove derivation checks. */
    drv.env.erase("allowedReferences");
    drv.env.erase("allowedRequisites");
    drv.env.erase("disallowedReferences");
    drv.env.erase("disallowedRequisites");

    /* Rehash and write the derivation. FIXME: would be nice to use
       'buildDerivation', but that's privileged. */
    drv.name += "-env";
    for (auto & output : drv.outputs) {
        output.second = { .output = DerivationOutputInputAddressed { .path = StorePath::dummy } };
        drv.env[output.first] = "";
    }
    drv.inputSrcs.insert(std::move(getEnvShPath));
    Hash h = std::get<0>(hashDerivationModulo(*store, drv, true));

    for (auto & output : drv.outputs) {
        auto outPath = store->makeOutputPath(output.first, h, drv.name);
        output.second = { .output = DerivationOutputInputAddressed { .path = outPath } };
        drv.env[output.first] = store->printStorePath(outPath);
    }

    auto shellDrvPath = writeDerivation(*store, drv);

    /* Build the derivation. */
    store->buildPaths({{shellDrvPath}});

    for (auto & [_0, outputAndOptPath] : drv.outputsAndOptPaths(*store)) {
        auto & [_1, optPath] = outputAndOptPath;
        assert(optPath);
        auto & outPath = *optPath;
        assert(store->isValidPath(outPath));
        auto outPathS = store->toRealPath(outPath);
        if (lstat(outPathS).st_size)
            return outPath;
    }

    throw Error("get-env.sh failed to produce an environment");
}

struct Common : InstallableCommand, MixProfile
{
    std::set<string> ignoreVars{
        "BASHOPTS",
        "EUID",
        "HOME", // FIXME: don't ignore in pure mode?
        "NIX_BUILD_TOP",
        "NIX_ENFORCE_PURITY",
        "NIX_LOG_FD",
        "PPID",
        "PWD",
        "SHELLOPTS",
        "SHLVL",
        "SSL_CERT_FILE", // FIXME: only want to ignore /no-cert-file.crt
        "TEMP",
        "TEMPDIR",
        "TERM",
        "TMP",
        "TMPDIR",
        "TZ",
        "UID",
    };

    std::string makeRcScript(
        const BuildEnvironment & buildEnvironment,
        const Path & outputsDir = absPath(".") + "/outputs")
    {
        std::ostringstream out;

        out << "unset shellHook\n";

        out << "nix_saved_PATH=\"$PATH\"\n";

        for (auto & i : buildEnvironment.env) {
            if (!ignoreVars.count(i.first) && !hasPrefix(i.first, "BASH_")) {
                if (i.second.associative)
                    out << fmt("declare -A %s=(%s)\n", i.first, i.second.quoted);
                else {
                    out << fmt("%s=%s\n", i.first, i.second.quoted);
                    if (i.second.exported)
                        out << fmt("export %s\n", i.first);
                }
            }
        }

        out << "PATH=\"$PATH:$nix_saved_PATH\"\n";

        out << buildEnvironment.bashFunctions << "\n";

        out << "export NIX_BUILD_TOP=\"$(mktemp -d --tmpdir nix-shell.XXXXXX)\"\n";
        for (auto & i : {"TMP", "TMPDIR", "TEMP", "TEMPDIR"})
            out << fmt("export %s=\"$NIX_BUILD_TOP\"\n", i);

        out << "eval \"$shellHook\"\n";

        /* Substitute occurrences of output paths. */
        auto outputs = buildEnvironment.env.find("outputs");
        assert(outputs != buildEnvironment.env.end());

        // FIXME: properly unquote 'outputs'.
        StringMap rewrites;
        for (auto & outputName : tokenizeString<std::vector<std::string>>(replaceStrings(outputs->second.quoted, "'", ""))) {
            auto from = buildEnvironment.env.find(outputName);
            assert(from != buildEnvironment.env.end());
            // FIXME: unquote
            rewrites.insert({from->second.quoted, outputsDir + "/" + outputName});
        }

        return rewriteStrings(out.str(), rewrites);
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {"devShell." + settings.thisSystem.get(), "defaultPackage." + settings.thisSystem.get()};
    }

    StorePath getShellOutPath(ref<Store> store)
    {
        auto path = installable->getStorePath();
        if (path && hasSuffix(path->to_string(), "-env"))
            return *path;
        else {
            auto drvs = toDerivations(store, {installable});

            if (drvs.size() != 1)
                throw Error("'%s' needs to evaluate to a single derivation, but it evaluated to %d derivations",
                    installable->what(), drvs.size());

            auto & drvPath = *drvs.begin();

            return getDerivationEnvironment(store, drvPath);
        }
    }

    std::pair<BuildEnvironment, std::string> getBuildEnvironment(ref<Store> store)
    {
        auto shellOutPath = getShellOutPath(store);

        auto strPath = store->printStorePath(shellOutPath);

        updateProfile(shellOutPath);

        return {readEnvironment(strPath), strPath};
    }
};

struct CmdDevelop : Common, MixEnvironment
{
    std::vector<std::string> command;
    std::optional<std::string> phase;

    CmdDevelop()
    {
        addFlag({
            .longName = "command",
            .shortName = 'c',
            .description = "command and arguments to be executed instead of an interactive shell",
            .labels = {"command", "args"},
            .handler = {[&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
            }}
        });

        addFlag({
            .longName = "phase",
            .description = "phase to run (e.g. `build` or `configure`)",
            .labels = {"phase-name"},
            .handler = {&phase},
        });

        addFlag({
            .longName = "configure",
            .description = "run the configure phase",
            .handler = {&phase, {"configure"}},
        });

        addFlag({
            .longName = "build",
            .description = "run the build phase",
            .handler = {&phase, {"build"}},
        });

        addFlag({
            .longName = "check",
            .description = "run the check phase",
            .handler = {&phase, {"check"}},
        });

        addFlag({
            .longName = "install",
            .description = "run the install phase",
            .handler = {&phase, {"install"}},
        });

        addFlag({
            .longName = "installcheck",
            .description = "run the installcheck phase",
            .handler = {&phase, {"installCheck"}},
        });
    }

    std::string description() override
    {
        return "run a bash shell that provides the build environment of a derivation";
    }

    Examples examples() override
    {
        return {
            Example{
                "To get the build environment of GNU hello:",
                "nix develop nixpkgs#hello"
            },
            Example{
                "To get the build environment of the default package of flake in the current directory:",
                "nix develop"
            },
            Example{
                "To store the build environment in a profile:",
                "nix develop --profile /tmp/my-shell nixpkgs#hello"
            },
            Example{
                "To use a build environment previously recorded in a profile:",
                "nix develop /tmp/my-shell"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto [buildEnvironment, gcroot] = getBuildEnvironment(store);

        auto [rcFileFd, rcFilePath] = createTempFile("nix-shell");

        auto script = makeRcScript(buildEnvironment);

        if (verbosity >= lvlDebug)
            script += "set -x\n";

        script += fmt("rm -f '%s'\n", rcFilePath);

        if (phase) {
            if (!command.empty())
                throw UsageError("you cannot use both '--command' and '--phase'");
            // FIXME: foundMakefile is set by buildPhase, need to get
            // rid of that.
            script += fmt("foundMakefile=1\n");
            script += fmt("runHook %1%Phase\n", *phase);
            script += fmt("exit 0\n", *phase);
        }

        else if (!command.empty()) {
            std::vector<std::string> args;
            for (auto s : command)
                args.push_back(shellEscape(s));
            script += fmt("exec %s\n", concatStringsSep(" ", args));
        }

        writeFull(rcFileFd.get(), script);

        stopProgressBar();

        setEnviron();
        // prevent garbage collection until shell exits
        setenv("NIX_GCROOT", gcroot.data(), 1);

        Path shell = "bash";

        try {
            auto state = getEvalState();

            auto bashInstallable = std::make_shared<InstallableFlake>(
                state,
                installable->nixpkgsFlakeRef(),
                Strings{"bashInteractive"},
                Strings{"legacyPackages." + settings.thisSystem.get() + "."},
                lockFlags);

            shell = state->store->printStorePath(
                toStorePath(state->store, Realise::Outputs, OperateOn::Output, bashInstallable)) + "/bin/bash";
        } catch (Error &) {
            ignoreException();
        }

        auto args = Strings{std::string(baseNameOf(shell)), "--rcfile", rcFilePath};

        restoreAffinity();
        restoreSignals();

        execvp(shell.c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing shell '%s'", shell);
    }
};

struct CmdPrintDevEnv : Common
{
    std::string description() override
    {
        return "print shell code that can be sourced by bash to reproduce the build environment of a derivation";
    }

    Examples examples() override
    {
        return {
            Example{
                "To apply the build environment of GNU hello to the current shell:",
                ". <(nix print-dev-env nixpkgs#hello)"
            },
        };
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        auto buildEnvironment = getBuildEnvironment(store).first;

        stopProgressBar();

        std::cout << makeRcScript(buildEnvironment);
    }
};

static auto r1 = registerCommand<CmdPrintDevEnv>("print-dev-env");
static auto r2 = registerCommand<CmdDevelop>("develop");

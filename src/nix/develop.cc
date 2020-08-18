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
    std::string value; // quoted string or array
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
            res.env.insert({match[1], Var { .exported = exported.count(match[1]) > 0, .value = match[2] }});
        }

        else if (std::regex_search(pos, file.cend(), match, assocArrayRegex, std::regex_constants::match_continuous)) {
            pos = match[0].second;
            res.env.insert({match[1], Var { .associative = true, .value = match[2] }});
        }

        else if (std::regex_search(pos, file.cend(), match, functionRegex, std::regex_constants::match_continuous)) {
            res.bashFunctions = std::string(pos, file.cend());
            break;
        }

        else throw Error("shell environment '%s' has unexpected line '%s'",
            path, file.substr(pos - file.cbegin(), 60));
    }

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
    for (auto & output : drv.outputs)
        drv.env.erase(output.first);
    drv.outputs = {{"out", DerivationOutput { .output = DerivationOutputInputAddressed { .path = StorePath::dummy }}}};
    drv.env["out"] = "";
    drv.env["_outputs_saved"] = drv.env["outputs"];
    drv.env["outputs"] = "out";
    drv.inputSrcs.insert(std::move(getEnvShPath));
    Hash h = std::get<0>(hashDerivationModulo(*store, drv, true));
    auto shellOutPath = store->makeOutputPath("out", h, drv.name);
    drv.outputs.insert_or_assign("out", DerivationOutput { .output = DerivationOutputInputAddressed {
                .path = shellOutPath
            } });
    drv.env["out"] = store->printStorePath(shellOutPath);
    auto shellDrvPath2 = writeDerivation(store, drv);

    /* Build the derivation. */
    store->buildPaths({{shellDrvPath2}});

    assert(store->isValidPath(shellOutPath));

    return shellOutPath;
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

    void makeRcScript(const BuildEnvironment & buildEnvironment, std::ostream & out)
    {
        out << "unset shellHook\n";

        out << "nix_saved_PATH=\"$PATH\"\n";

        for (auto & i : buildEnvironment.env) {
            if (!ignoreVars.count(i.first) && !hasPrefix(i.first, "BASH_")) {
                if (i.second.associative)
                    out << fmt("declare -A %s=(%s)\n", i.first, i.second.value);
                else {
                    out << fmt("%s=%s\n", i.first, i.second.value);
                    if (i.second.exported)
                        out << fmt("export %s\n", i.first);
                }
            }
        }

        out << "PATH=\"$PATH:$nix_saved_PATH\"\n";

        out << buildEnvironment.bashFunctions << "\n";

        // FIXME: set outputs

        out << "export NIX_BUILD_TOP=\"$(mktemp -d --tmpdir nix-shell.XXXXXX)\"\n";
        for (auto & i : {"TMP", "TMPDIR", "TEMP", "TEMPDIR"})
            out << fmt("export %s=\"$NIX_BUILD_TOP\"\n", i);

        out << "eval \"$shellHook\"\n";
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

    CmdDevelop()
    {
        addFlag({
            .longName = "command",
            .shortName = 'c',
            .description = "command and arguments to be executed insted of an interactive shell",
            .labels = {"command", "args"},
            .handler = {[&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
            }}
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

        std::ostringstream ss;
        makeRcScript(buildEnvironment, ss);

        ss << fmt("rm -f '%s'\n", rcFilePath);

        if (!command.empty()) {
            std::vector<std::string> args;
            for (auto s : command)
                args.push_back(shellEscape(s));
            ss << fmt("exec %s\n", concatStringsSep(" ", args));
        }

        writeFull(rcFileFd.get(), ss.str());

        stopProgressBar();

        setEnviron();
        // prevent garbage collection until shell exits
        setenv("NIX_GCROOT", gcroot.data(), 1);

        Path shell = "bash";

        try {
            auto state = getEvalState();

            auto bashInstallable = std::make_shared<InstallableFlake>(
                state,
                std::move(installable->nixpkgsFlakeRef()),
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

        makeRcScript(buildEnvironment, std::cout);
    }
};

static auto r1 = registerCommand<CmdPrintDevEnv>("print-dev-env");
static auto r2 = registerCommand<CmdDevelop>("develop");

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
    bool exported;
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

    static std::string arrayRegex =
        R"re((?:\(( *\[[^\]]+\]="(?:[^"\\]|\\.)*")*\)))re";

    static std::regex varRegex(
        "^(" + varNameRegex + ")=(" + simpleStringRegex + "|" + quotedStringRegex + "|" + arrayRegex + ")\n");

    static std::regex functionRegex(
        "^" + varNameRegex + " \\(\\) *\n");

    while (pos != file.end()) {

        std::smatch match;

        if (std::regex_search(pos, file.cend(), match, declareRegex)) {
            pos = match[0].second;
            exported.insert(match[1]);
        }

        else if (std::regex_search(pos, file.cend(), match, varRegex)) {
            pos = match[0].second;
            res.env.insert({match[1], Var { (bool) exported.count(match[1]), match[2] }});
        }

        else if (std::regex_search(pos, file.cend(), match, functionRegex)) {
            res.bashFunctions = std::string(pos, file.cend());
            break;
        }

        else throw Error("shell environment '%s' has unexpected line '%s'",
            path, file.substr(pos - file.cbegin(), 60));
    }

    return res;
}

/* Given an existing derivation, return the shell environment as
   initialised by stdenv's setup script. We do this by building a
   modified derivation with the same dependencies and nearly the same
   initial environment variables, that just writes the resulting
   environment to a file and exits. */
StorePath getDerivationEnvironment(ref<Store> store, Derivation drv)
{
    auto builder = baseNameOf(drv.builder);
    if (builder != "bash")
        throw Error("'nix dev-shell' only works on derivations that use 'bash' as their builder");

    drv.args = {
        "-c",
        "set -e; "
        "export IN_NIX_SHELL=impure; "
        "export dontAddDisableDepTrack=1; "
        "if [[ -n $stdenv ]]; then "
        "  source $stdenv/setup; "
        "fi; "
        "export > $out; "
        "set >> $out "};

    /* Remove derivation checks. */
    drv.env.erase("allowedReferences");
    drv.env.erase("allowedRequisites");
    drv.env.erase("disallowedReferences");
    drv.env.erase("disallowedRequisites");

    // FIXME: handle structured attrs

    /* Rehash and write the derivation. FIXME: would be nice to use
       'buildDerivation', but that's privileged. */
    auto drvName = drv.env["name"] + "-env";
    for (auto & output : drv.outputs)
        drv.env.erase(output.first);
    drv.env["out"] = "";
    drv.env["outputs"] = "out";
    Hash h = hashDerivationModulo(*store, drv, true);
    auto shellOutPath = store->makeOutputPath("out", h, drvName);
    drv.outputs.insert_or_assign("out", DerivationOutput(shellOutPath.clone(), "", ""));
    drv.env["out"] = store->printStorePath(shellOutPath);
    auto shellDrvPath2 = writeDerivation(store, drv, drvName);

    /* Build the derivation. */
    store->buildPaths({shellDrvPath2});

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
        out << "nix_saved_PATH=\"$PATH\"\n";

        for (auto & i : buildEnvironment.env) {
            if (!ignoreVars.count(i.first) && !hasPrefix(i.first, "BASH_")) {
                out << fmt("%s=%s\n", i.first, i.second.value);
                if (i.second.exported)
                    out << fmt("export %s\n", i.first);
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

    StorePath getShellOutPath(ref<Store> store)
    {
        auto path = installable->getStorePath();
        if (path && hasSuffix(path->to_string(), "-env"))
            return path->clone();
        else {
            auto drvs = toDerivations(store, {installable});

            if (drvs.size() != 1)
                throw Error("'%s' needs to evaluate to a single derivation, but it evaluated to %d derivations",
                    installable->what(), drvs.size());

            auto & drvPath = *drvs.begin();

            return getDerivationEnvironment(store, store->derivationFromPath(drvPath));
        }
    }

    BuildEnvironment getBuildEnvironment(ref<Store> store)
    {
        auto shellOutPath = getShellOutPath(store);

        updateProfile(shellOutPath);

        return readEnvironment(store->printStorePath(shellOutPath));
    }
};

struct CmdDevShell : Common, MixEnvironment
{
    std::vector<std::string> command;

    CmdDevShell()
    {
        mkFlag()
            .longName("command")
            .shortName('c')
            .description("command and arguments to be executed insted of an interactive shell")
            .labels({"command", "args"})
            .arity(ArityAny)
            .handler([&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
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
                "nix dev-shell nixpkgs.hello"
            },
            Example{
                "To store the build environment in a profile:",
                "nix dev-shell --profile /tmp/my-shell nixpkgs.hello"
            },
            Example{
                "To use a build environment previously recorded in a profile:",
                "nix dev-shell /tmp/my-shell"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto buildEnvironment = getBuildEnvironment(store);

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

        auto shell = getEnv("SHELL").value_or("bash");

        setEnviron();

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
                ". <(nix print-dev-env nixpkgs.hello)"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto buildEnvironment = getBuildEnvironment(store);

        stopProgressBar();

        makeRcScript(buildEnvironment, std::cout);
    }
};

static auto r1 = registerCommand<CmdPrintDevEnv>("print-dev-env");
static auto r2 = registerCommand<CmdDevShell>("dev-shell");

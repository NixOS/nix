#include "eval.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "progress-bar.hh"

using namespace nix;

struct BuildEnvironment
{
    // FIXME: figure out which vars should be exported.
    std::map<std::string, std::string> env;
    std::map<std::string, std::string> functions;
};

BuildEnvironment readEnvironment(const Path & path)
{
    BuildEnvironment res;

    auto lines = tokenizeString<Strings>(readFile(path), "\n");

    auto getLine =
        [&]() {
            if (lines.empty())
                throw Error("shell environment '%s' ends unexpectedly", path);
            auto line = lines.front();
            lines.pop_front();
            return line;
        };

    while (!lines.empty()) {
        auto line = getLine();

        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string name(line, 0, eq);
            std::string value(line, eq + 1);
            // FIXME: parse arrays
            res.env.insert({name, value});
        }

        else if (hasSuffix(line, " () ")) {
            std::string name(line, 0, line.size() - 4);
            // FIXME: validate name
            auto l = getLine();
            if (l != "{ ") throw Error("shell environment '%s' has unexpected line '%s'", path, l);
            std::string body;
            while ((l = getLine()) != "}") {
                body += l;
                body += '\n';
            }
            res.functions.insert({name, body});
        }

        else throw Error("shell environment '%s' has unexpected line '%s'", path, line);
    }

    return res;
}

/* Given an existing derivation, return the shell environment as
   initialised by stdenv's setup script. We do this by building a
   modified derivation with the same dependencies and nearly the same
   initial environment variables, that just writes the resulting
   environment to a file and exits. */
BuildEnvironment getDerivationEnvironment(ref<Store> store, Derivation drv)
{
    auto builder = baseNameOf(drv.builder);
    if (builder != "bash")
        throw Error("'nix shell' only works on derivations that use 'bash' as their builder");

    drv.args = {"-c", "set -e; if [[ -n $stdenv ]]; then source $stdenv/setup; fi; set > $out"};

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
    drv.outputs["out"] = DerivationOutput("", "", "");
    Hash h = hashDerivationModulo(*store, drv);
    Path shellOutPath = store->makeOutputPath("out", h, drvName);
    drv.outputs["out"].path = shellOutPath;
    drv.env["out"] = shellOutPath;
    Path shellDrvPath2 = writeDerivation(store, drv, drvName);

    /* Build the derivation. */
    store->buildPaths({shellDrvPath2});

    assert(store->isValidPath(shellOutPath));

    return readEnvironment(shellOutPath);
}

struct Common : InstallableCommand
{
    /*
    std::set<string> keepVars{
        "DISPLAY",
        "HOME",
        "IN_NIX_SHELL",
        "LOGNAME",
        "NIX_BUILD_SHELL",
        "PAGER",
        "PATH",
        "TERM",
        "TZ",
        "USER",
    };
    */

    std::set<string> ignoreVars{
        "BASHOPTS",
        "EUID",
        "HOME", // FIXME: don't ignore in pure mode?
        "NIX_BUILD_TOP",
        "NIX_ENFORCE_PURITY",
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
        out << "export IN_NIX_SHELL=1\n";
        out << "nix_saved_PATH=\"$PATH\"\n";

        for (auto & i : buildEnvironment.env) {
            // FIXME: shellEscape
            // FIXME: figure out what to export
            // FIXME: handle arrays
            if (!ignoreVars.count(i.first) && !hasPrefix(i.first, "BASH_"))
                out << fmt("export %s=%s\n", i.first, i.second);
        }

        out << "PATH=\"$PATH:$nix_saved_PATH\"\n";

        for (auto & i : buildEnvironment.functions) {
            out << fmt("%s () {\n%s\n}\n", i.first, i.second);
        }

        // FIXME: set outputs

        out << "export NIX_BUILD_TOP=\"$(mktemp -d --tmpdir nix-shell.XXXXXX)\"\n";
        for (auto & i : {"TMP", "TMPDIR", "TEMP", "TEMPDIR"})
            out << fmt("export %s=\"$NIX_BUILD_TOP\"\n", i);

        out << "eval \"$shellHook\"\n";
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {"devShell", "defaultPackage"};
    }
};

struct CmdDevShell : Common
{

    std::string name() override
    {
        return "dev-shell";
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
                "nix dev-shell nixpkgs:hello"
            },
            Example{
                "To get the build environment of the default package of flake in the current directory:",
                "nix dev-shell"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto drvs = toDerivations(store, {installable});

        if (drvs.size() != 1)
            throw Error("'%s' needs to evaluate to a single derivation, but it evaluated to %d derivations",
                installable->what(), drvs.size());

        auto & drvPath = *drvs.begin();

        auto buildEnvironment = getDerivationEnvironment(store, store->derivationFromPath(drvPath));

        auto [rcFileFd, rcFilePath] = createTempFile("nix-shell");

        std::ostringstream ss;
        makeRcScript(buildEnvironment, ss);

        ss << fmt("rm -f '%s'\n", rcFilePath);

        writeFull(rcFileFd.get(), ss.str());

        stopProgressBar();

        auto shell = getEnv("SHELL", "bash");

        auto args = Strings{baseNameOf(shell), "--rcfile", rcFilePath};

        restoreAffinity();
        restoreSignals();

        execvp(shell.c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing shell '%s'", shell);
    }
};

struct CmdPrintDevEnv : Common
{

    std::string name() override
    {
        return "print-dev-env";
    }

    std::string description() override
    {
        return "print shell code that can be sourced by bash to reproduce the build environment of a derivation";
    }

    Examples examples() override
    {
        return {
            Example{
                "To apply the build environment of GNU hello to the current shell:",
                ". <(nix print-dev-env nixpkgs:hello)"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto drvs = toDerivations(store, {installable});

        if (drvs.size() != 1)
            throw Error("'%s' needs to evaluate to a single derivation, but it evaluated to %d derivations",
                installable->what(), drvs.size());

        auto & drvPath = *drvs.begin();

        auto buildEnvironment = getDerivationEnvironment(store, store->derivationFromPath(drvPath));

        stopProgressBar();

        makeRcScript(buildEnvironment, std::cout);
    }
};

static RegisterCommand r1(make_ref<CmdPrintDevEnv>());
static RegisterCommand r2(make_ref<CmdDevShell>());

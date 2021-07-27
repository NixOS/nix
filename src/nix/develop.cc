#include "eval.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "path-with-outputs.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "progress-bar.hh"
#include "run.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct DevelopSettings : Config
{
    Setting<std::string> bashPrompt{this, "", "bash-prompt",
        "The bash prompt (`PS1`) in `nix develop` shells."};

    Setting<std::string> bashPromptSuffix{this, "", "bash-prompt-suffix",
        "Suffix appended to the `PS1` environment variable in `nix develop` shells."};
};

static DevelopSettings developSettings;

static GlobalConfig::Register rDevelopSettings(&developSettings);

struct BuildEnvironment
{
    struct String
    {
        bool exported;
        std::string value;

        bool operator == (const String & other) const
        {
            return exported == other.exported && value == other.value;
        }
    };

    using Array = std::vector<std::string>;

    using Associative = std::map<std::string, std::string>;

    using Value = std::variant<String, Array, Associative>;

    std::map<std::string, Value> vars;
    std::map<std::string, std::string> bashFunctions;

    static BuildEnvironment fromJSON(std::string_view in)
    {
        BuildEnvironment res;

        std::set<std::string> exported;

        auto json = nlohmann::json::parse(in);

        for (auto & [name, info] : json["variables"].items()) {
            std::string type = info["type"];
            if (type == "var" || type == "exported")
                res.vars.insert({name, BuildEnvironment::String { .exported = type == "exported", .value = info["value"] }});
            else if (type == "array")
                res.vars.insert({name, (Array) info["value"]});
            else if (type == "associative")
                res.vars.insert({name, (Associative) info["value"]});
        }

        for (auto & [name, def] : json["bashFunctions"].items()) {
            res.bashFunctions.insert({name, def});
        }

        return res;
    }

    std::string toJSON() const
    {
        auto res = nlohmann::json::object();

        auto vars2 = nlohmann::json::object();
        for (auto & [name, value] : vars) {
            auto info = nlohmann::json::object();
            if (auto str = std::get_if<String>(&value)) {
                info["type"] = str->exported ? "exported" : "var";
                info["value"] = str->value;
            }
            else if (auto arr = std::get_if<Array>(&value)) {
                info["type"] = "array";
                info["value"] = *arr;
            }
            else if (auto arr = std::get_if<Associative>(&value)) {
                info["type"] = "associative";
                info["value"] = *arr;
            }
            vars2[name] = std::move(info);
        }
        res["variables"] = std::move(vars2);

        res["bashFunctions"] = bashFunctions;

        auto json = res.dump();

        assert(BuildEnvironment::fromJSON(json) == *this);

        return json;
    }

    void toBash(std::ostream & out, const std::set<std::string> & ignoreVars) const
    {
        for (auto & [name, value] : vars) {
            if (!ignoreVars.count(name)) {
                if (auto str = std::get_if<String>(&value)) {
                    out << fmt("%s=%s\n", name, shellEscape(str->value));
                    if (str->exported)
                        out << fmt("export %s\n", name);
                }
                else if (auto arr = std::get_if<Array>(&value)) {
                    out << "declare -a " << name << "=(";
                    for (auto & s : *arr)
                        out << shellEscape(s) << " ";
                    out << ")\n";
                }
                else if (auto arr = std::get_if<Associative>(&value)) {
                    out << "declare -A " << name << "=(";
                    for (auto & [n, v] : *arr)
                        out << "[" << shellEscape(n) << "]=" << shellEscape(v) << " ";
                    out << ")\n";
                }
            }
        }

        for (auto & [name, def] : bashFunctions) {
            out << name << " ()\n{\n" << def << "}\n";
        }
    }

    static std::string getString(const Value & value)
    {
        if (auto str = std::get_if<String>(&value))
            return str->value;
        else
            throw Error("bash variable is not a string");
    }

    static Array getStrings(const Value & value)
    {
        if (auto str = std::get_if<String>(&value))
            return tokenizeString<Array>(str->value);
        else if (auto arr = std::get_if<Array>(&value)) {
            return *arr;
        } else if (auto assoc = std::get_if<Associative>(&value)) {
            Array assocKeys;
            std::for_each(assoc->begin(), assoc->end(), [&](auto & n) { assocKeys.push_back(n.first); });
            return assocKeys;
        }
        else
            throw Error("bash variable is not a string or array");
    }

    bool operator == (const BuildEnvironment & other) const
    {
        return vars == other.vars && bashFunctions == other.bashFunctions;
    }
};

const static std::string getEnvSh =
    #include "get-env.sh.gen.hh"
    ;

/* Given an existing derivation, return the shell environment as
   initialised by stdenv's setup script. We do this by building a
   modified derivation with the same dependencies and nearly the same
   initial environment variables, that just writes the resulting
   environment to a file and exits. */
static StorePath getDerivationEnvironment(ref<Store> store, ref<Store> evalStore, const StorePath & drvPath)
{
    auto drv = evalStore->derivationFromPath(drvPath);

    auto builder = baseNameOf(drv.builder);
    if (builder != "bash")
        throw Error("'nix develop' only works on derivations that use 'bash' as their builder");

    auto getEnvShPath = evalStore->addTextToStore("get-env.sh", getEnvSh, {});

    drv.args = {store->printStorePath(getEnvShPath)};

    /* Remove derivation checks. */
    drv.env.erase("allowedReferences");
    drv.env.erase("allowedRequisites");
    drv.env.erase("disallowedReferences");
    drv.env.erase("disallowedRequisites");

    /* Rehash and write the derivation. FIXME: would be nice to use
       'buildDerivation', but that's privileged. */
    drv.name += "-env";
    drv.inputSrcs.insert(std::move(getEnvShPath));
    if (settings.isExperimentalFeatureEnabled("ca-derivations")) {
        for (auto & output : drv.outputs) {
            output.second = {
                .output = DerivationOutputDeferred{},
            };
            drv.env[output.first] = hashPlaceholder(output.first);
        }
    } else {
        for (auto & output : drv.outputs) {
            output.second = { .output = DerivationOutputInputAddressed { .path = StorePath::dummy } };
            drv.env[output.first] = "";
        }
        Hash h = std::get<0>(hashDerivationModulo(*evalStore, drv, true));

        for (auto & output : drv.outputs) {
            auto outPath = store->makeOutputPath(output.first, h, drv.name);
            output.second = { .output = DerivationOutputInputAddressed { .path = outPath } };
            drv.env[output.first] = store->printStorePath(outPath);
        }
    }

    auto shellDrvPath = writeDerivation(*evalStore, drv);

    /* Build the derivation. */
    store->buildPaths({DerivedPath::Built{shellDrvPath}}, bmNormal, evalStore);

    for (auto & [_0, optPath] : evalStore->queryPartialDerivationOutputMap(shellDrvPath)) {
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
    std::set<std::string> ignoreVars{
        "BASHOPTS",
        "HOME", // FIXME: don't ignore in pure mode?
        "NIX_BUILD_TOP",
        "NIX_ENFORCE_PURITY",
        "NIX_LOG_FD",
        "NIX_REMOTE",
        "PPID",
        "SHELLOPTS",
        "SSL_CERT_FILE", // FIXME: only want to ignore /no-cert-file.crt
        "TEMP",
        "TEMPDIR",
        "TERM",
        "TMP",
        "TMPDIR",
        "TZ",
        "UID",
    };

    std::vector<std::pair<std::string, std::string>> redirects;

    Common()
    {
        addFlag({
            .longName = "redirect",
            .description = "Redirect a store path to a mutable location.",
            .labels = {"installable", "outputs-dir"},
            .handler = {[&](std::string installable, std::string outputsDir) {
                redirects.push_back({installable, outputsDir});
            }}
        });
    }

    std::string makeRcScript(
        ref<Store> store,
        const BuildEnvironment & buildEnvironment,
        const Path & outputsDir = absPath(".") + "/outputs")
    {
        std::ostringstream out;

        out << "unset shellHook\n";

        out << "nix_saved_PATH=\"$PATH\"\n";

        buildEnvironment.toBash(out, ignoreVars);

        out << "PATH=\"$PATH:$nix_saved_PATH\"\n";

        out << "export NIX_BUILD_TOP=\"$(mktemp -d -t nix-shell.XXXXXX)\"\n";
        for (auto & i : {"TMP", "TMPDIR", "TEMP", "TEMPDIR"})
            out << fmt("export %s=\"$NIX_BUILD_TOP\"\n", i);

        out << "eval \"$shellHook\"\n";

        auto script = out.str();

        /* Substitute occurrences of output paths. */
        auto outputs = buildEnvironment.vars.find("outputs");
        assert(outputs != buildEnvironment.vars.end());

        // FIXME: properly unquote 'outputs'.
        StringMap rewrites;
        for (auto & outputName : BuildEnvironment::getStrings(outputs->second)) {
            auto from = buildEnvironment.vars.find(outputName);
            assert(from != buildEnvironment.vars.end());
            // FIXME: unquote
            rewrites.insert({BuildEnvironment::getString(from->second), outputsDir + "/" + outputName});
        }

        /* Substitute redirects. */
        for (auto & [installable_, dir_] : redirects) {
            auto dir = absPath(dir_);
            auto installable = parseInstallable(store, installable_);
            auto builtPaths = toStorePaths(
                getEvalStore(), store, Realise::Nothing, OperateOn::Output, {installable});
            for (auto & path: builtPaths) {
                auto from = store->printStorePath(path);
                if (script.find(from) == std::string::npos)
                    warn("'%s' (path '%s') is not used by this build environment", installable->what(), from);
                else {
                    printInfo("redirecting '%s' to '%s'", from, dir);
                    rewrites.insert({from, dir});
                }
            }
        }

        return rewriteStrings(script, rewrites);
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {"devShell." + settings.thisSystem.get(), "defaultPackage." + settings.thisSystem.get()};
    }
    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        auto res = SourceExprCommand::getDefaultFlakeAttrPathPrefixes();
        res.emplace_front("devShells." + settings.thisSystem.get());
        return res;
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

            return getDerivationEnvironment(store, getEvalStore(), drvPath);
        }
    }

    std::pair<BuildEnvironment, std::string> getBuildEnvironment(ref<Store> store)
    {
        auto shellOutPath = getShellOutPath(store);

        auto strPath = store->printStorePath(shellOutPath);

        updateProfile(shellOutPath);

        debug("reading environment file '%s'", strPath);

        return {BuildEnvironment::fromJSON(readFile(store->toRealPath(shellOutPath))), strPath};
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
            .description = "Instead of starting an interactive shell, start the specified command and arguments.",
            .labels = {"command", "args"},
            .handler = {[&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
            }}
        });

        addFlag({
            .longName = "phase",
            .description = "The stdenv phase to run (e.g. `build` or `configure`).",
            .labels = {"phase-name"},
            .handler = {&phase},
        });

        addFlag({
            .longName = "configure",
            .description = "Run the `configure` phase.",
            .handler = {&phase, {"configure"}},
        });

        addFlag({
            .longName = "build",
            .description = "Run the `build` phase.",
            .handler = {&phase, {"build"}},
        });

        addFlag({
            .longName = "check",
            .description = "Run the `check` phase.",
            .handler = {&phase, {"check"}},
        });

        addFlag({
            .longName = "install",
            .description = "Run the `install` phase.",
            .handler = {&phase, {"install"}},
        });

        addFlag({
            .longName = "installcheck",
            .description = "Run the `installcheck` phase.",
            .handler = {&phase, {"installCheck"}},
        });
    }

    std::string description() override
    {
        return "run a bash shell that provides the build environment of a derivation";
    }

    std::string doc() override
    {
        return
          #include "develop.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto [buildEnvironment, gcroot] = getBuildEnvironment(store);

        auto [rcFileFd, rcFilePath] = createTempFile("nix-shell");

        auto script = makeRcScript(store, buildEnvironment);

        if (verbosity >= lvlDebug)
            script += "set -x\n";

        script += fmt("command rm -f '%s'\n", rcFilePath);

        if (phase) {
            if (!command.empty())
                throw UsageError("you cannot use both '--command' and '--phase'");
            // FIXME: foundMakefile is set by buildPhase, need to get
            // rid of that.
            script += fmt("foundMakefile=1\n");
            script += fmt("runHook %1%Phase\n", *phase);
        }

        else if (!command.empty()) {
            std::vector<std::string> args;
            for (auto s : command)
                args.push_back(shellEscape(s));
            script += fmt("exec %s\n", concatStringsSep(" ", args));
        }

        else {
            script = "[ -n \"$PS1\" ] && [ -e ~/.bashrc ] && source ~/.bashrc;\n" + script;
            if (developSettings.bashPrompt != "")
                script += fmt("[ -n \"$PS1\" ] && PS1=%s;\n", shellEscape(developSettings.bashPrompt));
            if (developSettings.bashPromptSuffix != "")
                script += fmt("[ -n \"$PS1\" ] && PS1+=%s;\n", shellEscape(developSettings.bashPromptSuffix));
        }

        writeFull(rcFileFd.get(), script);

        setEnviron();
        // prevent garbage collection until shell exits
        setenv("NIX_GCROOT", gcroot.data(), 1);

        Path shell = "bash";

        try {
            auto state = getEvalState();

            auto nixpkgsLockFlags = lockFlags;
            nixpkgsLockFlags.inputOverrides = {};
            nixpkgsLockFlags.inputUpdates = {};

            auto bashInstallable = std::make_shared<InstallableFlake>(
                this,
                state,
                installable->nixpkgsFlakeRef(),
                Strings{"bashInteractive"},
                Strings{"legacyPackages." + settings.thisSystem.get() + "."},
                nixpkgsLockFlags);

            shell = store->printStorePath(
                toStorePath(getEvalStore(), store, Realise::Outputs, OperateOn::Output, bashInstallable)) + "/bin/bash";
        } catch (Error &) {
            ignoreException();
        }

        // If running a phase or single command, don't want an interactive shell running after
        // Ctrl-C, so don't pass --rcfile
        auto args = phase || !command.empty() ? Strings{std::string(baseNameOf(shell)), rcFilePath}
            : Strings{std::string(baseNameOf(shell)), "--rcfile", rcFilePath};

        runProgramInStore(store, shell, args);
    }
};

struct CmdPrintDevEnv : Common, MixJSON
{
    std::string description() override
    {
        return "print shell code that can be sourced by bash to reproduce the build environment of a derivation";
    }

    std::string doc() override
    {
        return
          #include "print-dev-env.md"
          ;
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        auto buildEnvironment = getBuildEnvironment(store).first;

        stopProgressBar();

        logger->writeToStdout(
            json
            ? buildEnvironment.toJSON()
            : makeRcScript(store, buildEnvironment));
    }
};

static auto rCmdPrintDevEnv = registerCommand<CmdPrintDevEnv>("print-dev-env");
static auto rCmdDevelop = registerCommand<CmdDevelop>("develop");

#include "nix/util/config-global.hh"
#include "nix/expr/eval.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/cmd/command-installable-value.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/store/derivations.hh"

#ifndef _WIN32 // TODO re-enable on Windows
#  include "run.hh"
#endif

#include <iterator>
#include <memory>
#include <sstream>
#include <nlohmann/json.hpp>
#include <algorithm>

#include "nix/util/strings.hh"

namespace nix::fs {
using namespace std::filesystem;
}

using namespace nix;

struct DevelopSettings : Config
{
    Setting<std::string> bashPrompt{this, "", "bash-prompt", "The bash prompt (`PS1`) in `nix develop` shells."};

    Setting<std::string> bashPromptPrefix{
        this, "", "bash-prompt-prefix", "Prefix prepended to the `PS1` environment variable in `nix develop` shells."};

    Setting<std::string> bashPromptSuffix{
        this, "", "bash-prompt-suffix", "Suffix appended to the `PS1` environment variable in `nix develop` shells."};
};

static DevelopSettings developSettings;

static GlobalConfig::Register rDevelopSettings(&developSettings);

struct BuildEnvironment
{
    struct String
    {
        bool exported;
        std::string value;

        bool operator==(const String & other) const
        {
            return exported == other.exported && value == other.value;
        }
    };

    using Array = std::vector<std::string>;

    using Associative = StringMap;

    using Value = std::variant<String, Array, Associative>;

    std::map<std::string, Value> vars;
    StringMap bashFunctions;
    std::optional<std::pair<std::string, std::string>> structuredAttrs;

    static BuildEnvironment fromJSON(const nlohmann::json & json)
    {
        BuildEnvironment res;

        StringSet exported;

        for (auto & [name, info] : json["variables"].items()) {
            std::string type = info["type"];
            if (type == "var" || type == "exported")
                res.vars.insert(
                    {name, BuildEnvironment::String{.exported = type == "exported", .value = info["value"]}});
            else if (type == "array")
                res.vars.insert({name, (Array) info["value"]});
            else if (type == "associative")
                res.vars.insert({name, (Associative) info["value"]});
        }

        for (auto & [name, def] : json["bashFunctions"].items()) {
            res.bashFunctions.insert({name, def});
        }

        if (json.contains("structuredAttrs")) {
            res.structuredAttrs = {json["structuredAttrs"][".attrs.json"], json["structuredAttrs"][".attrs.sh"]};
        }

        return res;
    }

    static BuildEnvironment parseJSON(std::string_view in)
    {
        auto json = nlohmann::json::parse(in);

        return fromJSON(json);
    }

    nlohmann::json toJSON() const
    {
        auto res = nlohmann::json::object();

        auto vars2 = nlohmann::json::object();
        for (auto & [name, value] : vars) {
            auto info = nlohmann::json::object();
            if (auto str = std::get_if<String>(&value)) {
                info["type"] = str->exported ? "exported" : "var";
                info["value"] = str->value;
            } else if (auto arr = std::get_if<Array>(&value)) {
                info["type"] = "array";
                info["value"] = *arr;
            } else if (auto arr = std::get_if<Associative>(&value)) {
                info["type"] = "associative";
                info["value"] = *arr;
            }
            vars2[name] = std::move(info);
        }
        res["variables"] = std::move(vars2);

        res["bashFunctions"] = bashFunctions;

        if (providesStructuredAttrs()) {
            auto contents = nlohmann::json::object();
            contents[".attrs.sh"] = getAttrsSH();
            contents[".attrs.json"] = getAttrsJSON();
            res["structuredAttrs"] = std::move(contents);
        }

        assert(BuildEnvironment::fromJSON(res) == *this);

        return res;
    }

    bool providesStructuredAttrs() const
    {
        return structuredAttrs.has_value();
    }

    std::string getAttrsJSON() const
    {
        assert(providesStructuredAttrs());
        return structuredAttrs->first;
    }

    std::string getAttrsSH() const
    {
        assert(providesStructuredAttrs());
        return structuredAttrs->second;
    }

    void toBash(std::ostream & out, const StringSet & ignoreVars) const
    {
        for (auto & [name, value] : vars) {
            if (!ignoreVars.count(name)) {
                if (auto str = std::get_if<String>(&value)) {
                    out << fmt("%s=%s\n", name, escapeShellArgAlways(str->value));
                    if (str->exported)
                        out << fmt("export %s\n", name);
                } else if (auto arr = std::get_if<Array>(&value)) {
                    out << "declare -a " << name << "=(";
                    for (auto & s : *arr)
                        out << escapeShellArgAlways(s) << " ";
                    out << ")\n";
                } else if (auto arr = std::get_if<Associative>(&value)) {
                    out << "declare -A " << name << "=(";
                    for (auto & [n, v] : *arr)
                        out << "[" << escapeShellArgAlways(n) << "]=" << escapeShellArgAlways(v) << " ";
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

    static Associative getAssociative(const Value & value)
    {
        if (auto assoc = std::get_if<Associative>(&value))
            return *assoc;
        else
            throw Error("bash variable is not an associative array");
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
        } else
            throw Error("bash variable is not a string or array");
    }

    bool operator==(const BuildEnvironment & other) const
    {
        return vars == other.vars && bashFunctions == other.bashFunctions;
    }

    std::string getSystem() const
    {
        if (auto v = get(vars, "system"))
            return getString(*v);
        else
            return settings.thisSystem;
    }
};

const static std::string getEnvSh =
#include "get-env.sh.gen.hh"
    ;

/**
 * Given an existing derivation, return the shell environment as
 * initialised by stdenv's setup script. We do this by building a
 * modified derivation with the same dependencies and nearly the same
 * initial environment variables, that just writes the resulting
 * environment to a file and exits.
 */
static StorePath getDerivationEnvironment(ref<Store> store, ref<Store> evalStore, const StorePath & drvPath)
{
    auto drv = evalStore->derivationFromPath(drvPath);

    auto builder = baseNameOf(drv.builder);
    if (builder != "bash")
        throw Error("'nix develop' only works on derivations that use 'bash' as their builder");

    auto getEnvShPath = ({
        StringSource source{getEnvSh};
        evalStore->addToStoreFromDump(
            source,
            "get-env.sh",
            FileSerialisationMethod::Flat,
            ContentAddressMethod::Raw::Text,
            HashAlgorithm::SHA256,
            {});
    });

    drv.args = {store->printStorePath(getEnvShPath)};

    /* Remove derivation checks. */
    drv.env.erase("allowedReferences");
    drv.env.erase("allowedRequisites");
    drv.env.erase("disallowedReferences");
    drv.env.erase("disallowedRequisites");
    drv.env.erase("name");

    /* Rehash and write the derivation. FIXME: would be nice to use
       'buildDerivation', but that's privileged. */
    drv.name += "-env";
    drv.env.emplace("name", drv.name);
    drv.inputSrcs.insert(std::move(getEnvShPath));
    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
        for (auto & output : drv.outputs) {
            output.second = DerivationOutput::Deferred{}, drv.env[output.first] = hashPlaceholder(output.first);
        }
    } else {
        for (auto & output : drv.outputs) {
            output.second = DerivationOutput::Deferred{};
            drv.env[output.first] = "";
        }
        auto hashesModulo = hashDerivationModulo(*evalStore, drv, true);

        for (auto & output : drv.outputs) {
            Hash h = hashesModulo.hashes.at(output.first);
            auto outPath = store->makeOutputPath(output.first, h, drv.name);
            output.second = DerivationOutput::InputAddressed{
                .path = outPath,
            };
            drv.env[output.first] = store->printStorePath(outPath);
        }
    }

    auto shellDrvPath = writeDerivation(*evalStore, drv);

    /* Build the derivation. */
    store->buildPaths(
        {DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(shellDrvPath),
            .outputs = OutputsSpec::All{},
        }},
        bmNormal,
        evalStore);

    // `get-env.sh` will write its JSON output to an arbitrary output
    // path, so return the first non-empty output path.
    for (auto & [_0, optPath] : evalStore->queryPartialDerivationOutputMap(shellDrvPath)) {
        assert(optPath);
        auto & outPath = *optPath;
        auto st = store->getFSAccessor()->lstat(CanonPath(outPath.to_string()));
        if (st.fileSize.value_or(0))
            return outPath;
    }

    throw Error("get-env.sh failed to produce an environment");
}

struct Common : InstallableCommand, MixProfile
{
    StringSet ignoreVars{
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
            }},
        });
    }

    std::string makeRcScript(
        ref<Store> store,
        const BuildEnvironment & buildEnvironment,
        const std::filesystem::path & tmpDir,
        const std::filesystem::path & outputsDir = std::filesystem::path{std::filesystem::current_path()} / "outputs")
    {
        // A list of colon-separated environment variables that should be
        // prepended to, rather than overwritten, in order to keep the shell usable.
        // Please keep this list minimal in order to avoid impurities.
        static const char * const savedVars[] = {
            "PATH",          // for commands
            "XDG_DATA_DIRS", // for loadable completion
        };

        std::ostringstream out;

        out << "unset shellHook\n";

        for (auto & var : savedVars) {
            out << fmt("%s=${%s:-}\n", var, var);
            out << fmt("nix_saved_%s=\"$%s\"\n", var, var);
        }

        buildEnvironment.toBash(out, ignoreVars);

        for (auto & var : savedVars)
            out << fmt("%s=\"$%s${nix_saved_%s:+:$nix_saved_%s}\"\n", var, var, var, var);

        out << "export NIX_BUILD_TOP=\"$(mktemp -d -t nix-shell.XXXXXX)\"\n";
        for (auto & i : {"TMP", "TMPDIR", "TEMP", "TEMPDIR"})
            out << fmt("export %s=\"$NIX_BUILD_TOP\"\n", i);

        out << "eval \"${shellHook:-}\"\n";

        auto script = out.str();

        /* Substitute occurrences of output paths. */
        auto outputs = buildEnvironment.vars.find("outputs");
        assert(outputs != buildEnvironment.vars.end());

        StringMap rewrites;
        if (buildEnvironment.providesStructuredAttrs()) {
            for (auto & [outputName, from] : BuildEnvironment::getAssociative(outputs->second)) {
                rewrites.insert({from, (outputsDir / outputName).string()});
            }
        } else {
            for (auto & outputName : BuildEnvironment::getStrings(outputs->second)) {
                auto from = buildEnvironment.vars.find(outputName);
                assert(from != buildEnvironment.vars.end());
                rewrites.insert({
                    BuildEnvironment::getString(from->second),
                    (outputsDir / outputName).string(),
                });
            }
        }

        /* Substitute redirects. */
        for (auto & [installable_, dir_] : redirects) {
            auto dir = absPath(dir_);
            auto installable = parseInstallable(store, installable_);
            auto builtPaths =
                Installable::toStorePathSet(getEvalStore(), store, Realise::Nothing, OperateOn::Output, {installable});
            for (auto & path : builtPaths) {
                auto from = store->printStorePath(path);
                if (script.find(from) == std::string::npos)
                    warn("'%s' (path '%s') is not used by this build environment", installable->what(), from);
                else {
                    printInfo("redirecting '%s' to '%s'", from, dir);
                    rewrites.insert({from, dir});
                }
            }
        }

        if (buildEnvironment.providesStructuredAttrs()) {
            fixupStructuredAttrs(
                OS_STR("sh"), "NIX_ATTRS_SH_FILE", buildEnvironment.getAttrsSH(), rewrites, buildEnvironment, tmpDir);
            fixupStructuredAttrs(
                OS_STR("json"),
                "NIX_ATTRS_JSON_FILE",
                buildEnvironment.getAttrsJSON(),
                rewrites,
                buildEnvironment,
                tmpDir);
        }

        return rewriteStrings(script, rewrites);
    }

    /**
     * Replace the value of NIX_ATTRS_*_FILE (`/build/.attrs.*`) with a tmp file
     * that's accessible from the interactive shell session.
     */
    void fixupStructuredAttrs(
        PathViewNG::string_view ext,
        const std::string & envVar,
        const std::string & content,
        StringMap & rewrites,
        const BuildEnvironment & buildEnvironment,
        const std::filesystem::path & tmpDir)
    {
        auto targetFilePath = tmpDir / OS_STR(".attrs.");
        targetFilePath += ext;

        writeFile(targetFilePath, content);

        auto fileInBuilderEnv = buildEnvironment.vars.find(envVar);
        assert(fileInBuilderEnv != buildEnvironment.vars.end());
        rewrites.insert({BuildEnvironment::getString(fileInBuilderEnv->second), targetFilePath.string()});
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        Strings paths{
            "devShells." + settings.thisSystem.get() + ".default",
            "devShell." + settings.thisSystem.get(),
        };
        for (auto & p : SourceExprCommand::getDefaultFlakeAttrPaths())
            paths.push_back(p);
        return paths;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        auto res = SourceExprCommand::getDefaultFlakeAttrPathPrefixes();
        res.emplace_front("devShells." + settings.thisSystem.get() + ".");
        return res;
    }

    StorePath getShellOutPath(ref<Store> store, ref<Installable> installable)
    {
        auto path = installable->getStorePath();
        if (path && hasSuffix(path->to_string(), "-env"))
            return *path;
        else {
            auto drvs = Installable::toDerivations(store, {installable});

            if (drvs.size() != 1)
                throw Error(
                    "'%s' needs to evaluate to a single derivation, but it evaluated to %d derivations",
                    installable->what(),
                    drvs.size());

            auto & drvPath = *drvs.begin();

            return getDerivationEnvironment(store, getEvalStore(), drvPath);
        }
    }

    std::pair<BuildEnvironment, StorePath> getBuildEnvironment(ref<Store> store, ref<Installable> installable)
    {
        auto shellOutPath = getShellOutPath(store, installable);

        updateProfile(shellOutPath);

        debug("reading environment file '%s'", store->printStorePath(shellOutPath));

        return {BuildEnvironment::parseJSON(store->getFSAccessor()->readFile(shellOutPath.to_string())), shellOutPath};
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
                if (ss.empty())
                    throw UsageError("--command requires at least one argument");
                command = ss;
            }},
        });

        addFlag({
            .longName = "phase",
            .description = "The stdenv phase to run (e.g. `build` or `configure`).",
            .labels = {"phase-name"},
            .handler = {&phase},
        });

        addFlag({
            .longName = "unpack",
            .description = "Run the `unpack` phase.",
            .handler = {&phase, {"unpack"}},
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

    void run(ref<Store> store, ref<Installable> installable) override
    {
        auto [buildEnvironment, gcroot] = getBuildEnvironment(store, installable);

        auto [rcFileFd, rcFilePath] = createTempFile("nix-shell");

        AutoDelete tmpDir(createTempDir("", "nix-develop"), true);

        auto script = makeRcScript(store, buildEnvironment, tmpDir);

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
            args.reserve(command.size());
            for (const auto & s : command)
                args.push_back(escapeShellArgAlways(s));
            script += fmt("exec %s\n", concatStringsSep(" ", args));
        }

        else {
            script = "[ -n \"$PS1\" ] && [ -e ~/.bashrc ] && source ~/.bashrc;\nshopt -u expand_aliases\n" + script
                     + "\nshopt -s expand_aliases\n";
            if (developSettings.bashPrompt != "")
                script += fmt("[ -n \"$PS1\" ] && PS1=%s;\n", escapeShellArgAlways(developSettings.bashPrompt.get()));
            if (developSettings.bashPromptPrefix != "")
                script +=
                    fmt("[ -n \"$PS1\" ] && PS1=%s\"$PS1\";\n",
                        escapeShellArgAlways(developSettings.bashPromptPrefix.get()));
            if (developSettings.bashPromptSuffix != "")
                script +=
                    fmt("[ -n \"$PS1\" ] && PS1+=%s;\n", escapeShellArgAlways(developSettings.bashPromptSuffix.get()));
        }

        setEnviron();
        // prevent garbage collection until shell exits
        setEnv("NIX_GCROOT", store->printStorePath(gcroot).c_str());

        Path shell = "bash";
        bool foundInteractive = false;

        try {
            auto state = getEvalState();

            auto nixpkgsLockFlags = lockFlags;
            nixpkgsLockFlags.inputOverrides = {};
            nixpkgsLockFlags.inputUpdates = {};

            auto nixpkgs = defaultNixpkgsFlakeRef();
            if (auto * i = dynamic_cast<const InstallableFlake *>(&*installable))
                nixpkgs = i->nixpkgsFlakeRef();

            auto bashInstallable = make_ref<InstallableFlake>(
                nullptr, //< Don't barf when the command is run with --arg/--argstr
                state,
                std::move(nixpkgs),
                "bashInteractive",
                ExtendedOutputsSpec::Default(),
                Strings{},
                Strings{"legacyPackages." + settings.thisSystem.get() + "."},
                nixpkgsLockFlags);

            for (auto & path : Installable::toStorePathSet(
                     getEvalStore(), store, Realise::Outputs, OperateOn::Output, {bashInstallable})) {
                auto s = store->printStorePath(path) + "/bin/bash";
                if (pathExists(s)) {
                    shell = s;
                    foundInteractive = true;
                    break;
                }
            }

            if (!foundInteractive)
                throw Error("package 'nixpkgs#bashInteractive' does not provide a 'bin/bash'");

        } catch (Error &) {
            ignoreExceptionExceptInterrupt();
        }

        // Override SHELL with the one chosen for this environment.
        // This is to make sure the system shell doesn't leak into the build environment.
        setEnv("SHELL", shell.c_str());
        // https://github.com/NixOS/nix/issues/5873
        script += fmt("SHELL=\"%s\"\n", shell);
        if (foundInteractive)
            script += fmt("PATH=\"%s${PATH:+:$PATH}\"\n", std::filesystem::path(shell).parent_path());
        writeFull(rcFileFd.get(), script);

#ifdef _WIN32 // TODO re-enable on Windows
        throw UnimplementedError("Cannot yet spawn processes on Windows");
#else
        // If running a phase or single command, don't want an interactive shell running after
        // Ctrl-C, so don't pass --rcfile
        auto args = phase || !command.empty() ? Strings{std::string(baseNameOf(shell)), rcFilePath}
                                              : Strings{std::string(baseNameOf(shell)), "--rcfile", rcFilePath};

        // Need to chdir since phases assume in flake directory
        if (phase) {
            // chdir if installable is a flake of type git+file or path
            auto installableFlake = installable.dynamic_pointer_cast<InstallableFlake>();
            if (installableFlake) {
                auto sourcePath = installableFlake->getLockedFlake()->flake.resolvedRef.input.getSourcePath();
                if (sourcePath) {
                    if (chdir(sourcePath->c_str()) == -1) {
                        throw SysError("chdir to %s failed", *sourcePath);
                    }
                }
            }
        }

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        getEvalState()->evalCaches.clear();

        execProgramInStore(store, UseLookupPath::Use, shell, args, buildEnvironment.getSystem());
#endif
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

    Category category() override
    {
        return catUtility;
    }

    void run(ref<Store> store, ref<Installable> installable) override
    {
        auto buildEnvironment = getBuildEnvironment(store, installable).first;

        logger->stop();

        if (json) {
            printJSON(buildEnvironment.toJSON());
        } else {
            AutoDelete tmpDir(createTempDir("", "nix-dev-env"), true);
            logger->writeToStdout(makeRcScript(store, buildEnvironment, tmpDir));
        }
    }
};

static auto rCmdPrintDevEnv = registerCommand<CmdPrintDevEnv>("print-dev-env");
static auto rCmdDevelop = registerCommand<CmdDevelop>("develop");

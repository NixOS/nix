#include "globals.hh"
#include "installable-value.hh"
#include "installable-derived-path.hh"
#include "installable-attr-path.hh"
#include "installable-flake.hh"
#include "outputs-spec.hh"
#include "users.hh"
#include "util.hh"
#include "installable-derived-path.hh"
#include "command.hh"
#include "attr-path.hh"
#include "common-eval-args.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "shared.hh"
#include "flake/flake.hh"
#include "eval-cache.hh"
#include "url.hh"
#include "registry.hh"
#include "build-result.hh"

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>

namespace nix {

void completeFlakeInputPath(
    AddCompletions & completions,
    ref<EvalState> evalState,
    const std::vector<FlakeRef> & flakeRefs,
    std::string_view prefix)
{
    for (auto & flakeRef : flakeRefs) {
        auto flake = flake::getFlake(*evalState, flakeRef, true);
        for (auto & input : flake.inputs)
            if (hasPrefix(input.first, prefix))
                completions.add(input.first);
    }
}

MixFlakeOptions::MixFlakeOptions(AbstractArgs & args)
    : MixRepair(args)
    , HasEvalState(args)
{
    auto category = "Common flake-related options";

    args.addFlag({
        .longName = "recreate-lock-file",
        .description = R"(
    Recreate the flake's lock file from scratch.

    > **DEPRECATED**
    >
    > Use [`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md) instead.
        )",
        .category = category,
        .handler = {[&]() {
            lockFlags.recreateLockFile = true;
            warn("'--recreate-lock-file' is deprecated and will be removed in a future version; use 'nix flake update' instead.");
        }}
    });

    args.addFlag({
        .longName = "no-update-lock-file",
        .description = "Do not allow any updates to the flake's lock file.",
        .category = category,
        .handler = {&lockFlags.updateLockFile, false}
    });

    args.addFlag({
        .longName = "no-write-lock-file",
        .description = "Do not write the flake's newly generated lock file.",
        .category = category,
        .handler = {&lockFlags.writeLockFile, false}
    });

    args.addFlag({
        .longName = "no-registries",
        .description = R"(
    Don't allow lookups in the flake registries.

    > **DEPRECATED**
    >
    > Use [`--no-use-registries`](#opt-no-use-registries) instead.
        )",
        .category = category,
        .handler = {[&]() {
            lockFlags.useRegistries = false;
            warn("'--no-registries' is deprecated; use '--no-use-registries'");
        }}
    });

    args.addFlag({
        .longName = "commit-lock-file",
        .description = "Commit changes to the flake's lock file.",
        .category = category,
        .handler = {&lockFlags.commitLockFile, true}
    });

    args.addFlag({
        .longName = "update-input",
        .description = R"(
    Update a specific flake input (ignoring its previous entry in the lock file).

    > **DEPRECATED**
    >
    > Use [`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md) instead.
        )",
        .category = category,
        .labels = {"input-path"},
        .handler = {[&](std::string s) {
            warn("'--update-input' is a deprecated alias for 'flake update' and will be removed in a future version.");
            lockFlags.inputUpdates.insert(flake::parseInputPath(s));
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            completeFlakeInputPath(completions, getEvalState(), getFlakeRefsForCompletion(), prefix);
        }}
    });

    args.addFlag({
        .longName = "override-input",
        .description = "Override a specific flake input (e.g. `dwarffs/nixpkgs`). This implies `--no-write-lock-file`.",
        .category = category,
        .labels = {"input-path", "flake-url"},
        .handler = {[&](std::string inputPath, std::string flakeRef) {
            lockFlags.writeLockFile = false;
            lockFlags.inputOverrides.insert_or_assign(
                flake::parseInputPath(inputPath),
                parseFlakeRef(flakeRef, absPath(args.getCommandBaseDir()), true));
        }},
        .completer = {[&](AddCompletions & completions, size_t n, std::string_view prefix) {
            if (n == 0) {
                completeFlakeInputPath(completions, getEvalState(), getFlakeRefsForCompletion(), prefix);
            } else if (n == 1) {
                completeFlakeRef(completions, getEvalState()->store, prefix);
            }
        }}
    });

    args.addFlag({
        .longName = "reference-lock-file",
        .description = "Read the given lock file instead of `flake.lock` within the top-level flake.",
        .category = category,
        .labels = {"flake-lock-path"},
        .handler = {[&](std::string lockFilePath) {
            lockFlags.referenceLockFilePath = lockFilePath;
        }},
        .completer = AbstractArgs::completePath
    });

    args.addFlag({
        .longName = "output-lock-file",
        .description = "Write the given lock file instead of `flake.lock` within the top-level flake.",
        .category = category,
        .labels = {"flake-lock-path"},
        .handler = {[&](std::string lockFilePath) {
            lockFlags.outputLockFilePath = lockFilePath;
        }},
        .completer = AbstractArgs::completePath
    });

    args.addFlag({
        .longName = "inputs-from",
        .description = "Use the inputs of the specified flake as registry entries.",
        .category = category,
        .labels = {"flake-url"},
        .handler = {[&](std::string flakeRef) {
            auto evalState = getEvalState();
            auto flake = flake::lockFlake(
                *evalState,
                parseFlakeRef(flakeRef, absPath(args.getCommandBaseDir())),
                { .writeLockFile = false });
            for (auto & [inputName, input] : flake.lockFile.root->inputs) {
                auto input2 = flake.lockFile.findInput({inputName}); // resolve 'follows' nodes
                if (auto input3 = std::dynamic_pointer_cast<const flake::LockedNode>(input2)) {
                    overrideRegistry(
                        fetchers::Input::fromAttrs({{"type","indirect"}, {"id", inputName}}),
                        input3->lockedRef.input,
                        {});
                }
            }
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            completeFlakeRef(completions, getEvalState()->store, prefix);
        }}
    });
}

ParseInstallableValueArgs::ParseInstallableValueArgs(GetRawInstallables & args)
    : MixRepair(args)
    , HasEvalState(args)
    , MixFlakeOptions(args)
    , args(args)
{
    args.addFlag({
        .longName = "file",
        .shortName = 'f',
        .description =
            "Interpret [*installables*](@docroot@/command-ref/new-cli/nix.md#installables) as attribute paths relative to the Nix expression stored in *file*. "
            "If *file* is the character -, then a Nix expression will be read from standard input. "
            "Implies `--impure`.",
        .category = installablesCategory,
        .labels = {"file"},
        .handler = {&file},
        .completer = AbstractArgs::completePath
    });

    args.addFlag({
        .longName = "expr",
        .description = "Interpret [*installables*](@docroot@/command-ref/new-cli/nix.md#installables) as attribute paths relative to the Nix expression *expr*.",
        .category = installablesCategory,
        .labels = {"expr"},
        .handler = {&expr}
    });
}

MixReadOnlyOption::MixReadOnlyOption(AbstractArgs & args)
{
    args.addFlag({
        .longName = "read-only",
        .description =
            "Do not instantiate each evaluated derivation. "
            "This improves performance, but can cause errors when accessing "
            "store paths of derivations during evaluation.",
        .handler = {&settings.readOnlyMode, true},
    });
}

Strings ParseInstallableValueArgs::getDefaultFlakeAttrPaths()
{
    return {
        "packages." + settings.thisSystem.get() + ".default",
        "defaultPackage." + settings.thisSystem.get()
    };
}

Strings ParseInstallableValueArgs::getDefaultFlakeAttrPathPrefixes()
{
    return {
        // As a convenience, look for the attribute in
        // 'outputs.packages'.
        "packages." + settings.thisSystem.get() + ".",
        // As a temporary hack until Nixpkgs is properly converted
        // to provide a clean 'packages' set, look in 'legacyPackages'.
        "legacyPackages." + settings.thisSystem.get() + "."
    };
}

void ParseInstallableValueArgs::completeInstallable(AddCompletions & completions, std::string_view prefix)
{
    try {
        if (file) {
            completions.setType(AddCompletions::Type::Attrs);

            evalSettings.pureEval = false;
            auto state = getEvalState();
            Expr *e = state->parseExprFromFile(
                resolveExprPath(state->checkSourcePath(lookupFileArg(*state, *file)))
                );

            Value root;
            state->eval(e, root);

            auto autoArgs = getAutoArgs(*state);

            std::string prefix_ = std::string(prefix);
            auto sep = prefix_.rfind('.');
            std::string searchWord;
            if (sep != std::string::npos) {
                searchWord = prefix_.substr(sep + 1, std::string::npos);
                prefix_ = prefix_.substr(0, sep);
            } else {
                searchWord = prefix_;
                prefix_ = "";
            }

            auto [v, pos] = findAlongAttrPath(*state, prefix_, *autoArgs, root);
            Value &v1(*v);
            state->forceValue(v1, pos);
            Value v2;
            state->autoCallFunction(*autoArgs, v1, v2);

            if (v2.type() == nAttrs) {
                for (auto & i : *v2.attrs) {
                    std::string name = state->symbols[i.name];
                    if (name.find(searchWord) == 0) {
                        if (prefix_ == "")
                            completions.add(name);
                        else
                            completions.add(prefix_ + "." + name);
                    }
                }
            }
        } else {
            completeFlakeRefWithFragment(
                completions,
                getEvalState(),
                lockFlags,
                getDefaultFlakeAttrPathPrefixes(),
                getDefaultFlakeAttrPaths(),
                prefix);
        }
    } catch (EvalError&) {
        // Don't want eval errors to mess-up with the completion engine, so let's just swallow them
    }
}

void completeFlakeRefWithFragment(
    AddCompletions & completions,
    ref<EvalState> evalState,
    flake::LockFlags lockFlags,
    Strings attrPathPrefixes,
    const Strings & defaultFlakeAttrPaths,
    std::string_view prefix)
{
    /* Look for flake output attributes that match the
       prefix. */
    try {
        auto hash = prefix.find('#');
        if (hash == std::string::npos) {
            completeFlakeRef(completions, evalState->store, prefix);
        } else {
            completions.setType(AddCompletions::Type::Attrs);

            auto fragment = prefix.substr(hash + 1);
            std::string prefixRoot = "";
            if (fragment.starts_with(".")){
                fragment = fragment.substr(1);
                prefixRoot = ".";
            }
            auto flakeRefS = std::string(prefix.substr(0, hash));

            // TODO: ideally this would use the command base directory instead of assuming ".".
            auto flakeRef = parseFlakeRef(expandTilde(flakeRefS), absPath("."));

            auto evalCache = openEvalCache(*evalState,
                std::make_shared<flake::LockedFlake>(lockFlake(*evalState, flakeRef, lockFlags)));

            auto root = evalCache->getRoot();

            if (prefixRoot == "."){
                attrPathPrefixes.clear();
            }
            /* Complete 'fragment' relative to all the
               attrpath prefixes as well as the root of the
               flake. */
            attrPathPrefixes.push_back("");

            for (auto & attrPathPrefixS : attrPathPrefixes) {
                auto attrPathPrefix = parseAttrPath(*evalState, attrPathPrefixS);
                auto attrPathS = attrPathPrefixS + std::string(fragment);
                auto attrPath = parseAttrPath(*evalState, attrPathS);

                std::string lastAttr;
                if (!attrPath.empty() && !hasSuffix(attrPathS, ".")) {
                    lastAttr = evalState->symbols[attrPath.back()];
                    attrPath.pop_back();
                }

                auto attr = root->findAlongAttrPath(attrPath);
                if (!attr) continue;

                for (auto & attr2 : (*attr)->getAttrs()) {
                    if (hasPrefix(evalState->symbols[attr2], lastAttr)) {
                        auto attrPath2 = (*attr)->getAttrPath(attr2);
                        /* Strip the attrpath prefix. */
                        attrPath2.erase(attrPath2.begin(), attrPath2.begin() + attrPathPrefix.size());
                        completions.add(flakeRefS + "#" + prefixRoot + concatStringsSep(".", evalState->symbols.resolve(attrPath2)));
                    }
                }
            }

            /* And add an empty completion for the default
               attrpaths. */
            if (fragment.empty()) {
                for (auto & attrPath : defaultFlakeAttrPaths) {
                    auto attr = root->findAlongAttrPath(parseAttrPath(*evalState, attrPath));
                    if (!attr) continue;
                    completions.add(flakeRefS + "#" + prefixRoot);
                }
            }
        }
    } catch (Error & e) {
        warn(e.msg());
    }
}

void completeFlakeRef(AddCompletions & completions, ref<Store> store, std::string_view prefix)
{
    if (!experimentalFeatureSettings.isEnabled(Xp::Flakes))
        return;

    if (prefix == "")
        completions.add(".");

    Args::completeDir(completions, 0, prefix);

    /* Look for registry entries that match the prefix. */
    for (auto & registry : fetchers::getRegistries(store)) {
        for (auto & entry : registry->entries) {
            auto from = entry.from.to_string();
            if (!hasPrefix(prefix, "flake:") && hasPrefix(from, "flake:")) {
                std::string from2(from, 6);
                if (hasPrefix(from2, prefix))
                    completions.add(from2);
            } else {
                if (hasPrefix(from, prefix))
                    completions.add(from);
            }
        }
    }
}


ref<eval_cache::EvalCache> openEvalCache(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake)
{
    auto fingerprint = lockedFlake->getFingerprint();
    return make_ref<nix::eval_cache::EvalCache>(
        evalSettings.useEvalCache && evalSettings.pureEval
            ? std::optional { std::cref(fingerprint) }
            : std::nullopt,
        state,
        [&state, lockedFlake]()
        {
            /* For testing whether the evaluation cache is
               complete. */
            if (getEnv("NIX_ALLOW_EVAL").value_or("1") == "0")
                throw Error("not everything is cached, but evaluation is not allowed");

            auto vFlake = state.allocValue();
            flake::callFlake(state, *lockedFlake, *vFlake);

            state.forceAttrs(*vFlake, noPos, "while parsing cached flake data");

            auto aOutputs = vFlake->attrs->get(state.symbols.create("outputs"));
            assert(aOutputs);

            return aOutputs->value;
        });
}

Installables ParseInstallableValueArgs::parseInstallables(
    ref<Store> store, std::vector<std::string> ss)
{
    Installables result;

    if (file || expr) {
        if (file && expr)
            throw UsageError("'--file' and '--expr' are exclusive");

        // FIXME: backward compatibility hack
        if (file) evalSettings.pureEval = false;

        auto state = getEvalState();
        auto vFile = state->allocValue();

        if (file == "-") {
            auto e = state->parseStdin();
            state->eval(e, *vFile);
        }
        else if (file) {
            state->evalFile(lookupFileArg(*state, *file, CanonPath::fromCwd(args.getCommandBaseDir())), *vFile);
        }
        else {
            CanonPath dir(CanonPath::fromCwd(args.getCommandBaseDir()));
            auto e = state->parseExprFromString(*expr, state->rootPath(dir));
            state->eval(e, *vFile);
        }

        for (auto & s : ss) {
            auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(s);
            result.push_back(
                make_ref<InstallableAttrPath>(
                    InstallableAttrPath::parse(
                        state, *this, vFile, std::move(prefix), std::move(extendedOutputsSpec))));
        }

    } else {

        for (auto & s : ss) {
            std::exception_ptr ex;

            auto [prefix_, extendedOutputsSpec_] = ExtendedOutputsSpec::parse(s);
            // To avoid clang's pedantry
            auto prefix = std::move(prefix_);
            auto extendedOutputsSpec = std::move(extendedOutputsSpec_);

            if (prefix.find('/') != std::string::npos) {
                try {
                    result.push_back(make_ref<InstallableDerivedPath>(
                        InstallableDerivedPath::parse(store, prefix, extendedOutputsSpec.raw)));
                    continue;
                } catch (BadStorePath &) {
                } catch (...) {
                    if (!ex)
                        ex = std::current_exception();
                }
            }

            try {
                auto [flakeRef, fragment] = parseFlakeRefWithFragment(std::string { prefix }, absPath(args.getCommandBaseDir()));
                result.push_back(make_ref<InstallableFlake>(
                        this,
                        getEvalState(),
                        std::move(flakeRef),
                        fragment,
                        std::move(extendedOutputsSpec),
                        getDefaultFlakeAttrPaths(),
                        getDefaultFlakeAttrPathPrefixes(),
                        lockFlags));
                continue;
            } catch (...) {
                ex = std::current_exception();
            }

            std::rethrow_exception(ex);
        }
    }

    return result;
}

ref<Installable> ParseInstallableValueArgs::parseInstallable(
    ref<Store> store, const std::string & installable)
{
    auto installables = parseInstallables(store, {installable});
    assert(installables.size() == 1);
    return installables.front();
}

void ParseInstallableValueArgs::applyDefaultInstallables(std::vector<std::string> & rawInstallables)
{
    if (rawInstallables.empty()) {
        // FIXME: commands like "nix profile install" should not have a
        // default, probably.
        rawInstallables.push_back(".");
    }
}

std::vector<FlakeRef> ParseInstallableValueArgs::getFlakeRefsForCompletion()
{
    std::vector<FlakeRef> res;
    for (auto i : args.getRawInstallables())
        res.push_back(parseFlakeRefWithFragment(
            expandTilde(i),
            absPath(args.getCommandBaseDir())).first);
    return res;
}

void BuiltPathsCommand::applyDefaultInstallables(std::vector<std::string> & rawInstallables)
{
    if (rawInstallables.empty() && !all)
        rawInstallables.push_back(".");
}

}

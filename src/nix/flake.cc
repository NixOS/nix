#include "nix/cmd/common-eval-args.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/util/os-string.hh"
#include "nix/util/signals.hh"
#include "nix/util/mounted-source-accessor.hh"
#include "nix/store/store-open.hh"
#include "nix/store/derivations.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/expr/attr-path.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/registry.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/cmd/markdown.hh"
#include "nix/util/users.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/exit.hh"
#include "nix/cmd/flake-schemas.hh"
#include "nix/store/names.hh"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <iomanip>

#include "nix/util/strings-inline.hh"

// FIXME is this supposed to be private or not?
#include "flake-command.hh"

namespace nix {

struct CmdFlakeUpdate;

FlakeCommand::FlakeCommand()
{
    expectArgs(
        {.label = "flake-url",
         .optional = true,
         .handler = {&flakeUrl},
         .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
             completeFlakeRef(completions, getStore(), prefix);
         }}});
}

FlakeRef FlakeCommand::getFlakeRef()
{
    return parseFlakeRef(fetchSettings, flakeUrl, std::filesystem::current_path().string()); // FIXME
}

flake::LockedFlake FlakeCommand::lockFlake()
{
    return flake::lockFlake(flakeSettings, *getEvalState(), getFlakeRef(), lockFlags);
}

std::vector<FlakeRef> FlakeCommand::getFlakeRefsForCompletion()
{
    return {// Like getFlakeRef but with expandTilde called first
            parseFlakeRef(fetchSettings, expandTilde(flakeUrl), std::filesystem::current_path().string())};
}

struct CmdFlakeUpdate : FlakeCommand
{
public:

    std::string description() override
    {
        return "update flake lock file";
    }

    CmdFlakeUpdate()
    {
        expectedArgs.clear();
        addFlag({
            .longName = "flake",
            .description = "The flake to operate on. Default is the current directory.",
            .labels = {"flake-url"},
            .handler = {&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }},
        });
        expectArgs({
            .label = "inputs",
            .optional = true,
            .handler = {[&](std::vector<std::string> inputsToUpdate) {
                for (const auto & inputToUpdate : inputsToUpdate) {
                    std::optional<flake::NonEmptyInputAttrPath> inputAttrPath;
                    try {
                        inputAttrPath = flake::NonEmptyInputAttrPath::parse(inputToUpdate);
                        if (!inputAttrPath)
                            throw UsageError(
                                "input path to be updated cannot be zero-length; it would refer to the flake itself, not an input");
                    } catch (Error & e) {
                        warn(
                            "Invalid flake input '%s'. To update a specific flake, use 'nix flake update --flake %s' instead.",
                            inputToUpdate,
                            inputToUpdate);
                        throw e;
                    }
                    if (lockFlags.inputUpdates.contains(*inputAttrPath))
                        warn(
                            "Input '%s' was specified multiple times. You may have done this by accident.",
                            printInputAttrPath(*inputAttrPath));
                    lockFlags.inputUpdates.insert(*inputAttrPath);
                }
            }},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeInputAttrPath(completions, getEvalState(), getFlakeRefsForCompletion(), prefix);
            }},
        });

        /* Remove flags that don't make sense. */
        removeFlag("no-update-lock-file");
        removeFlag("no-write-lock-file");
    }

    std::string doc() override
    {
        return
#include "flake-update.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        fetchSettings.tarballTtl = 0;
        auto updateAll = lockFlags.inputUpdates.empty();

        lockFlags.recreateLockFile = updateAll;
        lockFlags.writeLockFile = true;
        lockFlags.applyNixConfig = true;

        lockFlake();
    }
};

struct CmdFlakeLock : FlakeCommand
{
    std::string description() override
    {
        return "create missing lock file entries";
    }

    CmdFlakeLock()
    {
        /* Remove flags that don't make sense. */
        removeFlag("no-write-lock-file");
    }

    std::string doc() override
    {
        return
#include "flake-lock.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        fetchSettings.tarballTtl = 0;

        lockFlags.writeLockFile = true;
        lockFlags.failOnUnlocked = true;
        lockFlags.applyNixConfig = true;

        lockFlake();
    }
};

struct CmdFlakeMetadata : FlakeCommand, MixJSON
{
    std::string description() override
    {
        return "show flake metadata";
    }

    std::string doc() override
    {
        return
#include "flake-metadata.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto lockedFlake = lockFlake();
        auto & flake = lockedFlake.flake;

        /* Flakes do not get copied to the store, but are instead mounted at
           their expected store paths in storeFS. Querying metadata does not
           force copying to the store, as one would expect. */
        auto storePath = store->toStorePath(flake.path.path.abs()).first;

        if (json) {
            nlohmann::json j;
            if (flake.description)
                j["description"] = *flake.description;
            j["originalUrl"] = flake.originalRef.to_string();
            j["original"] = fetchers::attrsToJSON(flake.originalRef.toAttrs());
            j["resolvedUrl"] = flake.resolvedRef.to_string();
            j["resolved"] = fetchers::attrsToJSON(flake.resolvedRef.toAttrs());
            j["url"] = flake.lockedRef.to_string(); // FIXME: rename to lockedUrl
            // "locked" is a misnomer - this is the result of the
            // attempt to lock.
            j["locked"] = fetchers::attrsToJSON(flake.lockedRef.toAttrs());
            if (auto rev = flake.lockedRef.input.getRev())
                j["revision"] = rev->to_string(HashFormat::Base16, false);
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                j["dirtyRevision"] = *dirtyRev;
            if (auto revCount = flake.lockedRef.input.getRevCount())
                j["revCount"] = *revCount;
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                j["lastModified"] = *lastModified;
            j["path"] = store->printStorePath(storePath);
            j["locks"] = lockedFlake.lockFile.toJSON().first;
            if (auto fingerprint = lockedFlake.getFingerprint(*store, fetchSettings))
                j["fingerprint"] = fingerprint->to_string(HashFormat::Base16, false);
            printJSON(j);
        } else {
            logger->cout(ANSI_BOLD "Resolved URL:" ANSI_NORMAL "  %s", flake.resolvedRef.to_string());
            if (flake.lockedRef.input.isLocked(fetchSettings))
                logger->cout(ANSI_BOLD "Locked URL:" ANSI_NORMAL "    %s", flake.lockedRef.to_string());
            if (flake.description)
                logger->cout(ANSI_BOLD "Description:" ANSI_NORMAL "   %s", *flake.description);
            logger->cout(ANSI_BOLD "Path:" ANSI_NORMAL "          %s", store->printStorePath(storePath));
            if (auto rev = flake.lockedRef.input.getRev())
                logger->cout(ANSI_BOLD "Revision:" ANSI_NORMAL "      %s", rev->to_string(HashFormat::Base16, false));
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                logger->cout(ANSI_BOLD "Revision:" ANSI_NORMAL "      %s", *dirtyRev);
            if (auto revCount = flake.lockedRef.input.getRevCount())
                logger->cout(ANSI_BOLD "Revisions:" ANSI_NORMAL "     %s", *revCount);
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                logger->cout(
                    ANSI_BOLD "Last modified:" ANSI_NORMAL " %s",
                    std::put_time(std::localtime(&*lastModified), "%F %T"));
            if (auto fingerprint = lockedFlake.getFingerprint(*store, fetchSettings))
                logger->cout(
                    ANSI_BOLD "Fingerprint:" ANSI_NORMAL "   %s", fingerprint->to_string(HashFormat::Base16, false));

            if (!lockedFlake.lockFile.root->inputs.empty())
                logger->cout(ANSI_BOLD "Inputs:" ANSI_NORMAL);

            std::set<ref<flake::Node>> visited{lockedFlake.lockFile.root};

            [&](this const auto & recurse, const flake::Node & node, const std::string & prefix) -> void {
                for (const auto & [last, input] : markLast(node.inputs)) {
                    if (auto lockedNode = std::get_if<0>(&input.second)) {
                        std::string lastModifiedStr = "";
                        if (auto lastModified = (*lockedNode)->lockedRef.input.getLastModified())
                            lastModifiedStr = fmt(" (%s)", std::put_time(std::gmtime(&*lastModified), "%F %T"));
                        logger->cout(
                            "%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s%s",
                            prefix + (last ? treeLast : treeConn),
                            input.first,
                            (*lockedNode)->lockedRef,
                            lastModifiedStr);

                        bool firstVisit = visited.insert(*lockedNode).second;

                        if (firstVisit)
                            recurse(**lockedNode, prefix + (last ? treeNull : treeLine));
                    } else if (auto follows = std::get_if<1>(&input.second)) {
                        logger->cout(
                            "%s" ANSI_BOLD "%s" ANSI_NORMAL " follows input '%s'",
                            prefix + (last ? treeLast : treeConn),
                            input.first,
                            flake::printInputAttrPath(*follows));
                    }
                }
            }(*lockedFlake.lockFile.root, "");
        }
    }
};

struct CmdFlakeInfo : CmdFlakeMetadata
{
    void run(nix::ref<nix::Store> store) override
    {
        warn("'nix flake info' is a deprecated alias for 'nix flake metadata'");
        CmdFlakeMetadata::run(store);
    }
};

/**
 * Log the current exception, after forcing cached evaluation errors.
 */
static void logEvalError()
{
    try {
        try {
            throw;
        } catch (eval_cache::CachedEvalError & e) {
            e.force();
        }
    } catch (Error & e) {
        logError(e.info());
    }
}

struct CmdFlakeCheck : FlakeCommand, MixFlakeSchemas
{
    bool build = true;
    bool buildAll = false;
    bool checkAllSystems = false;

    CmdFlakeCheck()
    {
        addFlag({
            .longName = "no-build",
            .description = "Do not build checks.",
            .handler = {&build, false},
        });
        addFlag({
            .longName = "build-all",
            .description = "Build all derivations, not just checks.",
            .handler = {&buildAll, true},
        });
        addFlag({
            .longName = "all-systems",
            .description = "Check the outputs for all systems.",
            .handler = {&checkAllSystems, true},
        });
    }

    std::string description() override
    {
        return "check whether the flake evaluates and run its tests";
    }

    std::string doc() override
    {
        return
#include "flake-check.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (!build) {
            settings.readOnlyMode = true;
            evalSettings.enableImportFromDerivation.setDefault(false);
        }

        auto state = getEvalState();

        lockFlags.applyNixConfig = true;
        auto flake = std::make_shared<flake::LockedFlake>(lockFlake());
        auto localSystem = std::string(settings.thisSystem.get());

        auto cache = flake_schemas::call(*state, flake, getDefaultFlakeSchemas());

        auto inventory = cache->getRoot()->getAttr("inventory");
        auto outputs = cache->getRoot()->getAttr("outputs");

        Sync<std::vector<DerivedPath>> drvPaths_;
        Sync<std::set<std::string>> uncheckedOutputs;
        Sync<std::set<std::string>> omittedSystems;
        Sync<std::map<DerivedPath, std::vector<AttrPath>>> derivedPathToAttrPaths_;

        std::function<void(ref<eval_cache::AttrCursor> node)> visit;

        std::atomic_bool hasErrors = false;

        visit = [&](ref<eval_cache::AttrCursor> node) {
            flake_schemas::visit(
                checkAllSystems ? std::optional<std::string>() : localSystem,
                false, // FIXME: add a --legacy flag?
                node,

                [&](const flake_schemas::Leaf & leaf) {
                    try {
                        bool done = true;
                        bool buildSkipped = false;

                        if (auto evalChecks = leaf.node->maybeGetAttr("evalChecks")) {
                            auto checkNames = evalChecks->getAttrs();
                            for (auto & checkName : checkNames) {
                                auto cursor = evalChecks->getAttr(checkName);
                                Activity act(
                                    *logger,
                                    lvlInfo,
                                    actUnknown,
                                    fmt("running flake check '%s'", cursor->getAttrPathStr()));
                                auto b = cursor->getBool();
                                if (!b)
                                    throw Error("Evaluation check '%s' failed.", cursor->getAttrPathStr());
                            }
                        }

                        if (auto drv = leaf.derivation(outputs)) {

                            /* Check whether this is a valid derivation. */
                            if (!drv->maybeGetAttr("drvPath") || drv->getAttr("type")->getString() != "derivation")
                                throw Error("Flake output '%s' is not a derivation.", drv->getAttrPathStr());

                            DrvName parsedDrvName(drv->getAttr("name")->getString());

                            if (buildAll || leaf.isFlakeCheck()) {
                                auto drvPath = drv->forceDerivation();
                                auto derivedPath = DerivedPath::Built{
                                    .drvPath = makeConstantStorePathRef(drvPath),
                                    .outputs = OutputsSpec::All{},
                                };
                                (*derivedPathToAttrPaths_.lock())[derivedPath].push_back(leaf.node->getAttrPath());
                                drvPaths_.lock()->push_back(std::move(derivedPath));
                                if (build)
                                    done = false;
                            } else
                                buildSkipped = true;
                        }

                        if (done)
                            notice(
                                "✅ " ANSI_BOLD "%s" ANSI_NORMAL "%s",
                                leaf.node->getAttrPathStr(),
                                buildSkipped ? ANSI_ITALIC ANSI_FAINT " (build skipped)" : "");
                    } catch (Interrupted & e) {
                        throw;
                    } catch (Error & e) {
                        printError("❌ " ANSI_RED "%s" ANSI_NORMAL, leaf.node->getAttrPathStr());
                        if (settings.getWorkerSettings().keepGoing) {
                            logEvalError();
                            hasErrors = true;
                        } else
                            throw;
                    }
                },

                [&](std::function<void(flake_schemas::ForEachChild)> forEachChild) {
                    forEachChild([&](Symbol attrName, ref<eval_cache::AttrCursor> node, bool isLast) { visit(node); });
                },

                [&](ref<eval_cache::AttrCursor> node, const std::vector<std::string> & systems) {
                    for (auto & s : systems)
                        omittedSystems.lock()->insert(s);
                },

                [&](ref<eval_cache::AttrCursor> node) {});
        };

        flake_schemas::forEachOutput(
            inventory,
            [&](Symbol outputName,
                std::shared_ptr<eval_cache::AttrCursor> output,
                const std::string & doc,
                bool isLast) {
                if (output)
                    visit(ref(output));
                else
                    uncheckedOutputs.lock()->insert(std::string(state->symbols[outputName]));
            });

        if (!uncheckedOutputs.lock()->empty())
            warn("The following flake outputs are unchecked: %s.", concatStringsSep(", ", *uncheckedOutputs.lock()));

        auto drvPaths(drvPaths_.lock());
        auto derivedPathToAttrPaths(derivedPathToAttrPaths_.lock());

        if (build && !drvPaths->empty()) {
            // TODO: This filtering of substitutable paths is a temporary workaround until
            // https://github.com/NixOS/nix/issues/5025 (union stores) is implemented.
            //
            // Once union stores are available, this code should be replaced with a proper
            // union store configuration. Ideally, we'd use a union of multiple destination
            // stores to preserve the current behavior where different substituters can
            // cache different check results.
            //
            // For now, we skip building derivations whose outputs are already available
            // via substitution, as `nix flake check` only needs to verify buildability,
            // not actually produce the outputs.
            auto missing = store->queryMissing(*drvPaths);

            std::vector<DerivedPath> toBuild;
            std::set<DerivedPath> toBuildSet;
            for (auto & path : missing.willBuild) {
                auto derivedPath = DerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(path),
                    .outputs = OutputsSpec::All{},
                };
                toBuild.emplace_back(derivedPath);
                toBuildSet.insert(std::move(derivedPath));
            }

            for (auto & [derivedPath, attrPaths] : *derivedPathToAttrPaths)
                if (!toBuildSet.contains(derivedPath))
                    for (auto & attrPath : attrPaths)
                        notice(
                            "✅ " ANSI_BOLD "%s" ANSI_NORMAL ANSI_ITALIC ANSI_FAINT " (previously built)" ANSI_NORMAL,
                            attrPath.to_string(*state));

            // FIXME: should start building while evaluating.
            Activity act(*logger, lvlInfo, actUnknown, fmt("running %d flake checks", toBuild.size()));

            auto buildResults = store->buildPathsWithResults(toBuild);

            for (auto & buildResult : buildResults) {
                if (auto failure = buildResult.tryGetFailure())
                    try {
                        hasErrors = true;
                        for (auto & attrPath : (*derivedPathToAttrPaths)[buildResult.path])
                            printError("❌ " ANSI_RED "%s" ANSI_NORMAL, attrPath.to_string(*state));
                        throw *failure;
                    } catch (Error & e) {
                        logError(e.info());
                    }
                else
                    for (auto & attrPath : (*derivedPathToAttrPaths)[buildResult.path])
                        notice("✅ " ANSI_BOLD "%s" ANSI_NORMAL, attrPath.to_string(*state));
            }
        }

        if (!omittedSystems.lock()->empty()) {
            // TODO: empty system is not visible; render all as nix strings?
            warn(
                "The check omitted these incompatible systems: %s\n"
                "Use '--all-systems' to check all.",
                concatStringsSep(", ", *omittedSystems.lock()));
        }

        if (hasErrors)
            throw Exit(1);
    };
};

struct CmdFlakeInitCommon : virtual Args, EvalCommand, MixFlakeSchemas
{
    std::string templateUrl = "templates";
    std::filesystem::path destDir;

    const flake::LockFlags lockFlags{.writeLockFile = false};

    CmdFlakeInitCommon()
    {
        addFlag({
            .longName = "template",
            .shortName = 't',
            .description = "The template to use.",
            .labels = {"template"},
            .handler = {&templateUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRefWithFragment(completions, getEvalState(), lockFlags, {"nix-template"}, prefix);
            }},
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flakeDir = absPath(destDir);

        auto evalState = getEvalState();

        auto [templateFlakeRef, templateName] =
            parseFlakeRefWithFragment(fetchSettings, templateUrl, std::filesystem::current_path().string());

        auto installable = InstallableFlake(
            nullptr,
            evalState,
            std::move(templateFlakeRef),
            templateName,
            ExtendedOutputsSpec::Default(),
            {"nix-template"},
            lockFlags,
            {});

        auto cursor = installable.getCursor(*evalState);

        auto templateDirAttr = cursor->getAttr("path")->forceValue();
        NixStringContext context;
        auto templateDir = evalState->coerceToPath(noPos, templateDirAttr, context, "");

        std::vector<std::filesystem::path> changedFiles;
        std::vector<std::filesystem::path> conflictedFiles;

        [&](this const auto & copyDir, const SourcePath & from, const std::filesystem::path & to) -> void {
            createDirs(to);

            for (auto & [name, entry] : from.readDirectory()) {
                checkInterrupt();
                auto from2 = from / name;
                auto to2 = to / name;
                auto st = from2.lstat();
                auto to_st = std::filesystem::symlink_status(to2);
                if (st.type == SourceAccessor::tDirectory)
                    copyDir(from2, to2);
                else if (st.type == SourceAccessor::tRegular) {
                    auto contents = from2.readFile();
                    if (std::filesystem::exists(to_st)) {
                        auto contents2 = readFile(to2);
                        if (contents != contents2) {
                            printError(
                                "refusing to overwrite existing file %s\n please merge it manually with '%s'",
                                PathFmt(to2),
                                from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        writeFile(to2, contents);
                } else if (st.type == SourceAccessor::tSymlink) {
                    auto target = from2.readLink();
                    if (std::filesystem::exists(to_st)) {
                        if (std::filesystem::read_symlink(to2) != target) {
                            printError(
                                "refusing to overwrite existing file %s\n please merge it manually with '%s'",
                                PathFmt(to2),
                                from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        createSymlink(target, to2);
                } else
                    throw Error(
                        "path '%s' needs to be a symlink, file, or directory but instead is a %s",
                        from2,
                        st.typeString());
                changedFiles.push_back(to2);
                notice("wrote: %s", PathFmt(to2));
            }
        }(templateDir, flakeDir);

        if (!changedFiles.empty() && std::filesystem::exists(std::filesystem::path{flakeDir} / ".git")) {
            OsStrings args = {
                OS_STR("-C"),
                flakeDir.native(),
                OS_STR("add"),
                OS_STR("--intent-to-add"),
                OS_STR("--force"),
                OS_STR("--"),
            };
            for (auto & s : changedFiles)
                args.emplace_back(s.native());
            runProgram("git", true, args);
        }

        if (auto welcomeText = cursor->maybeGetAttr("welcomeText")) {
            notice("\n");
            notice(renderMarkdownToTerminal(welcomeText->getString()));
        }

        if (!conflictedFiles.empty())
            throw Error("encountered %d conflicts - see above", conflictedFiles.size());
    }
};

struct CmdFlakeInit : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the current directory from a template";
    }

    std::string doc() override
    {
        return
#include "flake-init.md"
            ;
    }

    CmdFlakeInit()
    {
        destDir = ".";
    }
};

struct CmdFlakeNew : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the specified directory from a template";
    }

    std::string doc() override
    {
        return
#include "flake-new.md"
            ;
    }

    CmdFlakeNew()
    {
        expectArgs({.label = "dest-dir", .handler = {&destDir}, .completer = completePath});
    }
};

struct CmdFlakeClone : FlakeCommand
{
    std::filesystem::path destDir;

    std::string description() override
    {
        return "clone flake repository";
    }

    std::string doc() override
    {
        return
#include "flake-clone.md"
            ;
    }

    CmdFlakeClone()
    {
        addFlag({
            .longName = "dest",
            .shortName = 'f',
            .description = "Clone the flake to path *dest*.",
            .labels = {"path"},
            .handler = {&destDir},
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (destDir.empty())
            throw Error("missing flag '--dest'");

        getFlakeRef().resolve(fetchSettings, *store).input.clone(fetchSettings, *store, destDir);
    }
};

struct CmdFlakeArchive : FlakeCommand, MixJSON, MixDryRun, MixNoCheckSigs
{
    std::optional<StoreReference> dstUri;

    SubstituteFlag substitute = NoSubstitute;

    CmdFlakeArchive()
    {
        addFlag({
            .longName = "to",
            .description = "URI of the destination Nix store",
            .labels = {"store-uri"},
            .handler = {[this](std::string s) { dstUri = StoreReference::parse(s); }},
        });
    }

    std::string description() override
    {
        return "copy a flake and all its inputs to a store";
    }

    std::string doc() override
    {
        return
#include "flake-archive.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = lockFlake();

        StorePathSet sources;

        auto getStorePath = [&](const FlakeRef & lockedRef) {
            return dryRun ? lockedRef.input.computeStorePath(*store)
                          : std::get<StorePath>(lockedRef.input.fetchToStore(fetchSettings, *store));
        };

        auto storePath = getStorePath(flake.flake.lockedRef);

        sources.insert(storePath);

        // FIXME: use graph output, handle cycles.
        auto traverse = [&store, json = json, &sources, &getStorePath](
                            this const auto & self, const flake::Node & node) -> nlohmann::json {
            nlohmann::json jsonObj2 = json ? nlohmann::json::object() : nlohmann::json(nullptr);
            for (auto & [inputName, input] : node.inputs) {
                if (auto inputNode = std::get_if<0>(&input)) {
                    std::optional<StorePath> storePath;
                    const auto & lockedRef = (*inputNode)->lockedRef;
                    if (!lockedRef.input.isRelative()) {
                        storePath = getStorePath(lockedRef);
                        sources.insert(*storePath);
                    }
                    if (json) {
                        auto & jsonObj3 = jsonObj2[inputName];
                        if (storePath)
                            jsonObj3["path"] = store->printStorePath(*storePath);
                        jsonObj3["inputs"] = self(**inputNode);
                    } else
                        self(**inputNode);
                }
            }
            return jsonObj2;
        };

        if (json) {
            nlohmann::json jsonRoot = {
                {"path", store->printStorePath(storePath)},
                {"inputs", traverse(*flake.lockFile.root)},
            };
            printJSON(jsonRoot);
        } else {
            traverse(*flake.lockFile.root);
        }

        if (!dryRun && dstUri) {
            ref<Store> dstStore = openStore(StoreReference{*dstUri});

            copyPaths(*store, *dstStore, sources, NoRepair, checkSigs, substitute);
        }
    }
};

struct CmdFlakeShow : FlakeCommand, MixJSON, MixFlakeSchemas
{
    bool showLegacy = false;
    bool showAllSystems = false;
    bool showOutputPaths = false;
    bool showDrvPaths = false;
    bool showDrvNames = false;

    CmdFlakeShow()
    {
        addFlag({
            .longName = "legacy",
            .description = "Show the contents of the `legacyPackages` output.",
            .handler = {&showLegacy, true},
        });
        addFlag({
            .longName = "all-systems",
            .description = "Show the contents of outputs for all systems.",
            .handler = {&showAllSystems, true},
        });
        addFlag({
            .longName = "output-paths",
            .description = "Include the store paths of derivation outputs in the JSON output.",
            .handler = {&showOutputPaths, true},
        });
        addFlag({
            .longName = "drv-paths",
            .description = "Include the store paths of derivations in the JSON output.",
            .handler = {&showDrvPaths, true},
        });
        addFlag({
            .longName = "drv-names",
            .description = "Show the names and versions of derivations.",
            .handler = {&showDrvNames, true},
        });
    }

    std::string description() override
    {
        return "show the outputs provided by a flake";
    }

    std::string doc() override
    {
        return
#include "flake-show.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (showOutputPaths && !json)
            throw UsageError("The '--output-paths' flag requires '--json'.");

        if (showDrvPaths && !json)
            throw UsageError("The '--drv-paths' flag requires '--json'.");

        auto state = getEvalState();
        auto flake = make_ref<flake::LockedFlake>(lockFlake());
        auto localSystem = std::string(settings.thisSystem.get());

        auto cache = flake_schemas::call(*state, flake, getDefaultFlakeSchemas());

        auto inventory = cache->getRoot()->getAttr("inventory");
        auto outputs = cache->getRoot()->getAttr("outputs");

        std::function<void(ref<eval_cache::AttrCursor> node, nlohmann::json & obj)> visit;

        visit = [&](ref<eval_cache::AttrCursor> node, nlohmann::json & obj) {
            flake_schemas::visit(
                showAllSystems ? std::optional<std::string>() : localSystem,
                showLegacy,
                node,

                [&](const flake_schemas::Leaf & leaf) {
                    if (auto what = leaf.what())
                        obj.emplace("what", *what);

                    if (auto shortDescription = leaf.shortDescription())
                        obj.emplace("shortDescription", *shortDescription);

                    if (auto drv = leaf.derivation(outputs)) {
                        auto drvObj = nlohmann::json::object();

                        if (json || showDrvNames)
                            drvObj.emplace("name", drv->getAttr(state->s.name)->getString());

                        if (showDrvPaths) {
                            auto drvPath = drv->forceDerivation();
                            drvObj.emplace("path", store->printStorePath(drvPath));
                        }

                        if (showOutputPaths) {
                            auto outputs = nlohmann::json::object();
                            auto drvPath = drv->forceDerivation();
                            auto drv = getEvalStore()->derivationFromPath(drvPath);
                            for (auto & i : drv.outputsAndOptPaths(*store)) {
                                if (auto outPath = i.second.second)
                                    outputs.emplace(i.first, store->printStorePath(*outPath));
                                else
                                    outputs.emplace(i.first, nullptr);
                            }
                            drvObj.emplace("outputs", std::move(outputs));
                        }

                        obj.emplace("derivation", std::move(drvObj));
                    }

                    if (auto forSystems = leaf.forSystems())
                        obj.emplace("forSystems", *forSystems);
                },

                [&](std::function<void(flake_schemas::ForEachChild)> forEachChild) {
                    auto children = nlohmann::json::object();
                    forEachChild([&](Symbol attrName, ref<eval_cache::AttrCursor> node, bool isLast) {
                        auto & j = children.emplace(state->symbols[attrName], nlohmann::json::object()).first.value();
                        {
                            try {
                                visit(node, j);
                            } catch (EvalError & e) {
                                // FIXME: make it a flake schema attribute whether to ignore evaluation errors.
                                if (node->root->state.symbols[node->getAttrPath()[0]] == "legacyPackages")
                                    j.emplace("failed", true);
                                else
                                    throw;
                            }
                        }
                    });
                    obj.emplace("children", std::move(children));
                },

                [&](ref<eval_cache::AttrCursor> node, const std::vector<std::string> & systems) {
                    obj.emplace("filtered", true);
                },

                [&](ref<eval_cache::AttrCursor> node) { obj.emplace("isLegacy", true); });
        };

        auto inv = nlohmann::json::object();

        flake_schemas::forEachOutput(
            inventory,
            [&](Symbol outputName,
                std::shared_ptr<eval_cache::AttrCursor> output,
                const std::string & doc,
                bool isLast) {
                auto & j = inv.emplace(state->symbols[outputName], nlohmann::json::object()).first.value();

                if (output) {
                    j.emplace("doc", doc);
                    auto & j2 = j.emplace("output", nlohmann::json::object()).first.value();
                    visit(ref(output), j2);
                } else
                    j.emplace("unknown", true);
            });

        if (json) {
            auto res = nlohmann::json{{"version", 2}, {"inventory", std::move(inv)}};
            printJSON(res);
        } else {

            // Render the JSON into a tree representation.
            std::function<void(nlohmann::json j, const std::string & headerPrefix, const std::string & nextPrefix)>
                render;

            render = [&](nlohmann::json j, const std::string & headerPrefix, const std::string & nextPrefix) {
                auto what = j.find("what");
                auto filtered = j.find("filtered");
                auto isLegacy = j.find("isLegacy");
                auto derivation = j.find("derivation");

                auto s = headerPrefix;

                if (what != j.end())
                    s += fmt(": %s", (std::string) *what);

                if (derivation != j.end()) {
                    auto name = derivation->find("name");
                    if (name != derivation->end())
                        s += fmt(ANSI_ITALIC " [%s]" ANSI_NORMAL, (std::string) *name);
                }

                if (filtered != j.end() && (bool) *filtered)
                    s += " " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--all-systems' to show)";

                if (isLegacy != j.end() && (bool) *isLegacy)
                    s += " " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--legacy' to show)";

                logger->cout(s);

                auto children = j.find("children");

                if (children != j.end()) {
                    for (const auto & [i, child] : enumerate(children->items())) {
                        bool last = i + 1 == children->size();
                        render(
                            child.value(),
                            fmt(ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL,
                                nextPrefix,
                                last ? treeLast : treeConn,
                                child.key()),
                            nextPrefix + (last ? treeNull : treeLine));
                    }
                }
            };

            logger->cout("%s", fmt(ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef));

            for (const auto & [i, child] : enumerate(inv.items())) {
                bool last = i + 1 == inv.size();
                auto nextPrefix = last ? treeNull : treeLine;
                auto output = child.value().find("output");
                auto headerPrefix = fmt(
                    ANSI_GREEN "%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL, last ? treeLast : treeConn, child.key());
                if (output != child.value().end())
                    render(*output, headerPrefix, nextPrefix);
                else if (child.value().contains("unknown"))
                    logger->cout(headerPrefix + ANSI_WARNING " unknown flake output" ANSI_NORMAL);
            }
        }
    }
};

struct CmdFlakePrefetch : FlakeCommand, MixJSON
{
    std::optional<std::filesystem::path> outLink;

    CmdFlakePrefetch()
    {
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Create symlink named *path* to the resulting store path.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath,
        });
    }

    std::string description() override
    {
        return "download the source tree denoted by a flake reference into the Nix store";
    }

    std::string doc() override
    {
        return
#include "flake-prefetch.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto originalRef = getFlakeRef();
        auto resolvedRef = originalRef.resolve(fetchSettings, *store);
        auto [accessor, lockedRef] = resolvedRef.lazyFetch(getEvalState()->fetchSettings, *store);
        auto storePath =
            fetchToStore(getEvalState()->fetchSettings, *store, accessor, FetchMode::Copy, lockedRef.input.getName());
        auto hash = store->queryPathInfo(storePath)->narHash;

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(HashFormat::SRI, true);
            res["original"] = fetchers::attrsToJSON(resolvedRef.toAttrs());
            res["locked"] = fetchers::attrsToJSON(lockedRef.toAttrs());
            res["locked"].erase("__final"); // internal for now
            printJSON(res);
        } else {
            notice(
                "Downloaded '%s' to '%s' (hash '%s').",
                lockedRef.to_string(),
                store->printStorePath(storePath),
                hash.to_string(HashFormat::SRI, true));
        }

        if (outLink) {
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                createOutLinks(*outLink, {BuiltPath::Opaque{storePath}}, *store2);
            else
                throw Error("'--out-link' is not supported for this Nix store");
        }
    }
};

struct CmdFlake : NixMultiCommand
{
    CmdFlake()
        : NixMultiCommand("flake", RegisterCommand::getCommandsFor({"flake"}))
    {
    }

    std::string description() override
    {
        return "manage Nix flakes";
    }

    std::string doc() override
    {
        return
#include "flake.md"
            ;
    }

    void run() override
    {
        experimentalFeatureSettings.require(Xp::Flakes);
        NixMultiCommand::run();
    }
};

static auto rCmdFlake = registerCommand<CmdFlake>("flake");
static auto rCmdFlakeArchive = registerCommand2<CmdFlakeArchive>({"flake", "archive"});
static auto rCmdFlakeCheck = registerCommand2<CmdFlakeCheck>({"flake", "check"});
static auto rCmdFlakeClone = registerCommand2<CmdFlakeClone>({"flake", "clone"});
static auto rCmdFlakeInfo = registerCommand2<CmdFlakeInfo>({"flake", "info"});
static auto rCmdFlakeInit = registerCommand2<CmdFlakeInit>({"flake", "init"});
static auto rCmdFlakeLock = registerCommand2<CmdFlakeLock>({"flake", "lock"});
static auto rCmdFlakeMetadata = registerCommand2<CmdFlakeMetadata>({"flake", "metadata"});
static auto rCmdFlakeNew = registerCommand2<CmdFlakeNew>({"flake", "new"});
static auto rCmdFlakePrefetch = registerCommand2<CmdFlakePrefetch>({"flake", "prefetch"});
static auto rCmdFlakeShow = registerCommand2<CmdFlakeShow>({"flake", "show"});
static auto rCmdFlakeUpdate = registerCommand2<CmdFlakeUpdate>({"flake", "update"});

} // namespace nix

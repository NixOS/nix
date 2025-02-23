#include "command.hh"
#include "installable-flake.hh"
#include "common-args.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "eval-settings.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "signals.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "outputs-spec.hh"
#include "attr-path.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "eval-cache.hh"
#include "markdown.hh"
#include "users.hh"
#include "fetch-to-store.hh"
#include "local-fs-store.hh"
#include "flake-schemas.hh"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <iomanip>

#include "strings-inline.hh"

namespace nix::fs { using namespace std::filesystem; }

using namespace nix;
using namespace nix::flake;
using json = nlohmann::json;

struct CmdFlakeUpdate;
class FlakeCommand : virtual Args, public MixFlakeOptions
{
protected:
    std::string flakeUrl = ".";

public:

    FlakeCommand()
    {
        expectArgs({
            .label = "flake-url",
            .optional = true,
            .handler = {&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }}
        });
    }

    FlakeRef getFlakeRef()
    {
        return parseFlakeRef(fetchSettings, flakeUrl, fs::current_path().string()); //FIXME
    }

    LockedFlake lockFlake()
    {
        return flake::lockFlake(flakeSettings, *getEvalState(), getFlakeRef(), lockFlags);
    }

    std::vector<FlakeRef> getFlakeRefsForCompletion() override
    {
        return {
            // Like getFlakeRef but with expandTilde calld first
            parseFlakeRef(fetchSettings, expandTilde(flakeUrl), fs::current_path().string())
        };
    }
};

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
            .longName="flake",
            .description="The flake to operate on. Default is the current directory.",
            .labels={"flake-url"},
            .handler={&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }}
        });
        expectArgs({
            .label="inputs",
            .optional=true,
            .handler={[&](std::vector<std::string> inputsToUpdate){
                for (const auto & inputToUpdate : inputsToUpdate) {
                    InputAttrPath inputAttrPath;
                    try {
                        inputAttrPath = flake::parseInputAttrPath(inputToUpdate);
                    } catch (Error & e) {
                        warn("Invalid flake input '%s'. To update a specific flake, use 'nix flake update --flake %s' instead.", inputToUpdate, inputToUpdate);
                        throw e;
                    }
                    if (lockFlags.inputUpdates.contains(inputAttrPath))
                        warn("Input '%s' was specified multiple times. You may have done this by accident.", printInputAttrPath(inputAttrPath));
                    lockFlags.inputUpdates.insert(inputAttrPath);
                }
            }},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeInputAttrPath(completions, getEvalState(), getFlakeRefsForCompletion(), prefix);
            }}
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
        settings.tarballTtl = 0;
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
        settings.tarballTtl = 0;

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

        // Currently, all flakes are in the Nix store via the rootFS accessor.
        auto storePath = store->printStorePath(store->toStorePath(flake.path.path.abs()).first);

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
            j["path"] = storePath;
            j["locks"] = lockedFlake.lockFile.toJSON().first;
            if (auto fingerprint = lockedFlake.getFingerprint(store, fetchSettings))
                j["fingerprint"] = fingerprint->to_string(HashFormat::Base16, false);
            logger->cout("%s", j.dump());
        } else {
            logger->cout(
                ANSI_BOLD "Resolved URL:" ANSI_NORMAL "  %s",
                flake.resolvedRef.to_string());
            if (flake.lockedRef.input.isLocked())
                logger->cout(
                    ANSI_BOLD "Locked URL:" ANSI_NORMAL "    %s",
                    flake.lockedRef.to_string());
            if (flake.description)
                logger->cout(
                    ANSI_BOLD "Description:" ANSI_NORMAL "   %s",
                    *flake.description);
            logger->cout(
                ANSI_BOLD "Path:" ANSI_NORMAL "          %s",
                storePath);
            if (auto rev = flake.lockedRef.input.getRev())
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    rev->to_string(HashFormat::Base16, false));
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    *dirtyRev);
            if (auto revCount = flake.lockedRef.input.getRevCount())
                logger->cout(
                    ANSI_BOLD "Revisions:" ANSI_NORMAL "     %s",
                    *revCount);
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                logger->cout(
                    ANSI_BOLD "Last modified:" ANSI_NORMAL " %s",
                    std::put_time(std::localtime(&*lastModified), "%F %T"));
            if (auto fingerprint = lockedFlake.getFingerprint(store, fetchSettings))
                logger->cout(
                    ANSI_BOLD "Fingerprint:" ANSI_NORMAL "   %s",
                    fingerprint->to_string(HashFormat::Base16, false));

            if (!lockedFlake.lockFile.root->inputs.empty())
                logger->cout(ANSI_BOLD "Inputs:" ANSI_NORMAL);

            std::set<ref<Node>> visited;

            std::function<void(const Node & node, const std::string & prefix)> recurse;

            recurse = [&](const Node & node, const std::string & prefix)
            {
                for (const auto & [i, input] : enumerate(node.inputs)) {
                    bool last = i + 1 == node.inputs.size();

                    if (auto lockedNode = std::get_if<0>(&input.second)) {
                        std::string lastModifiedStr = "";
                        if (auto lastModified = (*lockedNode)->lockedRef.input.getLastModified())
                            lastModifiedStr = fmt(" (%s)", std::put_time(std::gmtime(&*lastModified), "%F %T"));
                        logger->cout("%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s%s",
                            prefix + (last ? treeLast : treeConn), input.first,
                            (*lockedNode)->lockedRef,
                            lastModifiedStr);

                        bool firstVisit = visited.insert(*lockedNode).second;

                        if (firstVisit) recurse(**lockedNode, prefix + (last ? treeNull : treeLine));
                    } else if (auto follows = std::get_if<1>(&input.second)) {
                        logger->cout("%s" ANSI_BOLD "%s" ANSI_NORMAL " follows input '%s'",
                            prefix + (last ? treeLast : treeConn), input.first,
                            printInputAttrPath(*follows));
                    }
                }
            };

            visited.insert(lockedFlake.lockFile.root);
            recurse(*lockedFlake.lockFile.root, "");
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

struct CmdFlakeCheck : FlakeCommand, flake_schemas::MixFlakeSchemas
{
    bool build = true;
    bool checkAllSystems = false;

    CmdFlakeCheck()
    {
        addFlag({
            .longName = "no-build",
            .description = "Do not build checks.",
            .handler = {&build, false}
        });
        addFlag({
            .longName = "all-systems",
            .description = "Check the outputs for all systems.",
            .handler = {&checkAllSystems, true}
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
        auto flake = std::make_shared<LockedFlake>(lockFlake());
        auto localSystem = std::string(settings.thisSystem.get());

        auto [cache, inventory] = flake_schemas::call(*state, flake, getDefaultFlakeSchemas());

        std::vector<DerivedPath> drvPaths;

        std::set<std::string> uncheckedOutputs;
        std::set<std::string> omittedSystems;

        std::function<void(ref<eval_cache::AttrCursor> node)> visit;

        bool hasErrors = false;

        auto reportError = [&](const Error & e) {
            try {
                throw e;
            } catch (Interrupted & e) {
                throw;
            } catch (Error & e) {
                if (settings.keepGoing) {
                    logError({.msg = e.info().msg});
                    hasErrors = true;
                }
                else
                    throw;
            }
        };

        visit = [&](ref<eval_cache::AttrCursor> node)
        {
            flake_schemas::visit(
                checkAllSystems ? std::optional<std::string>() : localSystem,
                node,

                [&](ref<eval_cache::AttrCursor> leaf)
                {
                    if (auto evalChecks = leaf->maybeGetAttr("evalChecks")) {
                        auto checkNames = evalChecks->getAttrs();
                        for (auto & checkName : checkNames) {
                            // FIXME: update activity
                            auto cursor = evalChecks->getAttr(checkName);
                            auto b = cursor->getBool();
                            if (!b)
                                reportError(Error("Evaluation check '%s' failed.", flake_schemas::toAttrPathStr(cursor)));
                        }
                    }

                    if (auto drv = flake_schemas::derivation(leaf)) {
                        if (auto isFlakeCheck = leaf->maybeGetAttr("isFlakeCheck")) {
                            if (isFlakeCheck->getBool()) {
                                auto drvPath = drv->forceDerivation();
                                drvPaths.push_back(DerivedPath::Built {
                                    .drvPath = makeConstantStorePathRef(drvPath),
                                    .outputs = OutputsSpec::All { },
                                });
                            }
                        }
                    }
                },

                [&](std::function<void(flake_schemas::ForEachChild)> forEachChild)
                {
                    forEachChild([&](Symbol attrName, ref<eval_cache::AttrCursor> node, bool isLast)
                    {
                        visit(node);
                    });
                },

                [&](ref<eval_cache::AttrCursor> node, const std::vector<std::string> & systems) {
                    for (auto & s : systems)
                        omittedSystems.insert(s);
                });
        };

        flake_schemas::forEachOutput(inventory, [&](Symbol outputName, std::shared_ptr<eval_cache::AttrCursor> output, const std::string & doc, bool isLast)
        {
            if (output) {
                visit(ref(output));
            } else
                uncheckedOutputs.insert(std::string(state->symbols[outputName]));
        });

        if (!uncheckedOutputs.empty())
            warn("The following flake outputs are unchecked: %s.",
                concatStringsSep(", ", uncheckedOutputs)); // FIXME: quote

        if (build && !drvPaths.empty()) {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("running %d flake checks", drvPaths.size()));
            store->buildPaths(drvPaths);
        }

        if (hasErrors)
            throw Error("some errors were encountered during the evaluation");

        if (!omittedSystems.empty()) {
            // TODO: empty system is not visible; render all as nix strings?
            warn(
                "The check omitted these incompatible systems: %s\n"
                "Use '--all-systems' to check all.",
                concatStringsSep(", ", omittedSystems)
            );
        }
    };
};

static Strings defaultTemplateAttrPathsPrefixes{"templates."};
static Strings defaultTemplateAttrPaths = {"templates.default", "defaultTemplate"};

struct CmdFlakeInitCommon : virtual Args, EvalCommand
{
    std::string templateUrl = "templates";
    Path destDir;

    const LockFlags lockFlags{ .writeLockFile = false };

    CmdFlakeInitCommon()
    {
        addFlag({
            .longName = "template",
            .shortName = 't',
            .description = "The template to use.",
            .labels = {"template"},
            .handler = {&templateUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRefWithFragment(
                    completions,
                    getEvalState(),
                    lockFlags,
                    defaultTemplateAttrPathsPrefixes,
                    defaultTemplateAttrPaths,
                    prefix);
            }}
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flakeDir = absPath(destDir);

        auto evalState = getEvalState();

        auto [templateFlakeRef, templateName] = parseFlakeRefWithFragment(
            fetchSettings, templateUrl, fs::current_path().string());

        auto installable = InstallableFlake(nullptr,
            evalState, std::move(templateFlakeRef), templateName, ExtendedOutputsSpec::Default(),
            defaultTemplateAttrPaths,
            defaultTemplateAttrPathsPrefixes,
            lockFlags);

        auto cursor = installable.getCursor(*evalState);

        auto templateDirAttr = cursor->getAttr("path")->forceValue();
        NixStringContext context;
        auto templateDir = evalState->coerceToPath(noPos, templateDirAttr, context, "");

        std::vector<fs::path> changedFiles;
        std::vector<fs::path> conflictedFiles;

        std::function<void(const SourcePath & from, const fs::path & to)> copyDir;
        copyDir = [&](const SourcePath & from, const fs::path & to)
        {
            fs::create_directories(to);

            for (auto & [name, entry] : from.readDirectory()) {
                checkInterrupt();
                auto from2 = from / name;
                auto to2 = to / name;
                auto st = from2.lstat();
                auto to_st = fs::symlink_status(to2);
                if (st.type == SourceAccessor::tDirectory)
                    copyDir(from2, to2);
                else if (st.type == SourceAccessor::tRegular) {
                    auto contents = from2.readFile();
                    if (fs::exists(to_st)) {
                        auto contents2 = readFile(to2.string());
                        if (contents != contents2) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2.string(), from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        writeFile(to2, contents);
                }
                else if (st.type == SourceAccessor::tSymlink) {
                    auto target = from2.readLink();
                    if (fs::exists(to_st)) {
                        if (fs::read_symlink(to2) != target) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2.string(), from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        createSymlink(target, os_string_to_string(PathViewNG { to2 }));
                }
                else
                    throw Error("path '%s' needs to be a symlink, file, or directory but instead is a %s", from2, st.typeString());
                changedFiles.push_back(to2);
                notice("wrote: %s", to2);
            }
        };

        copyDir(templateDir, flakeDir);

        if (!changedFiles.empty() && fs::exists(std::filesystem::path{flakeDir} / ".git")) {
            Strings args = { "-C", flakeDir, "add", "--intent-to-add", "--force", "--" };
            for (auto & s : changedFiles) args.emplace_back(s.string());
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
        expectArgs({
            .label = "dest-dir",
            .handler = {&destDir},
            .completer = completePath
        });
    }
};

struct CmdFlakeClone : FlakeCommand
{
    Path destDir;

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
            .handler = {&destDir}
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (destDir.empty())
            throw Error("missing flag '--dest'");

        getFlakeRef().resolve(store).input.clone(destDir);
    }
};

struct CmdFlakeArchive : FlakeCommand, MixJSON, MixDryRun
{
    std::string dstUri;

    CmdFlakeArchive()
    {
        addFlag({
            .longName = "to",
            .description = "URI of the destination Nix store",
            .labels = {"store-uri"},
            .handler = {&dstUri}
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

        auto storePath = store->toStorePath(flake.flake.path.path.abs()).first;

        sources.insert(storePath);

        // FIXME: use graph output, handle cycles.
        std::function<nlohmann::json(const Node & node)> traverse;
        traverse = [&](const Node & node)
        {
            nlohmann::json jsonObj2 = json ? json::object() : nlohmann::json(nullptr);
            for (auto & [inputName, input] : node.inputs) {
                if (auto inputNode = std::get_if<0>(&input)) {
                    auto storePath =
                        dryRun
                        ? (*inputNode)->lockedRef.input.computeStorePath(*store)
                        : (*inputNode)->lockedRef.input.fetchToStore(store).first;
                    if (json) {
                        auto& jsonObj3 = jsonObj2[inputName];
                        jsonObj3["path"] = store->printStorePath(storePath);
                        sources.insert(std::move(storePath));
                        jsonObj3["inputs"] = traverse(**inputNode);
                    } else {
                        sources.insert(std::move(storePath));
                        traverse(**inputNode);
                    }
                }
            }
            return jsonObj2;
        };

        if (json) {
            nlohmann::json jsonRoot = {
                {"path", store->printStorePath(storePath)},
                {"inputs", traverse(*flake.lockFile.root)},
            };
            logger->cout("%s", jsonRoot);
        } else {
            traverse(*flake.lockFile.root);
        }

        if (!dryRun && !dstUri.empty()) {
            ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);
            copyPaths(*store, *dstStore, sources);
        }
    }
};

struct CmdFlakeShow : FlakeCommand, MixJSON, flake_schemas::MixFlakeSchemas
{
    bool showLegacy = false;
    bool showAllSystems = false;

    CmdFlakeShow()
    {
        addFlag({
            .longName = "legacy",
            .description = "Show the contents of the `legacyPackages` output.",
            .handler = {&showLegacy, true}
        });
        addFlag({
            .longName = "all-systems",
            .description = "Show the contents of outputs for all systems.",
            .handler = {&showAllSystems, true}
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
        auto state = getEvalState();
        auto flake = std::make_shared<LockedFlake>(lockFlake());
        auto localSystem = std::string(settings.thisSystem.get());

        auto [cache, inventory] = flake_schemas::call(*state, flake, getDefaultFlakeSchemas());

        if (json) {
            std::function<void(ref<eval_cache::AttrCursor> node, nlohmann::json & obj)> visit;

            visit = [&](ref<eval_cache::AttrCursor> node, nlohmann::json & obj)
            {
                flake_schemas::visit(
                    showAllSystems ? std::optional<std::string>() : localSystem,
                    node,

                    [&](ref<eval_cache::AttrCursor> leaf)
                    {
                        obj.emplace("leaf", true);

                        if (auto what = flake_schemas::what(leaf))
                            obj.emplace("what", *what);

                        if (auto shortDescription = flake_schemas::shortDescription(leaf))
                            obj.emplace("shortDescription", *shortDescription);

                        if (auto drv = flake_schemas::derivation(leaf))
                            obj.emplace("derivationName", drv->getAttr(state->sName)->getString());

                        // FIXME: add more stuff
                    },

                    [&](std::function<void(flake_schemas::ForEachChild)> forEachChild)
                    {
                        auto children = nlohmann::json::object();
                        forEachChild([&](Symbol attrName, ref<eval_cache::AttrCursor> node, bool isLast)
                        {
                            auto j = nlohmann::json::object();
                            try {
                                visit(node, j);
                            } catch (EvalError & e) {
                                // FIXME: make it a flake schema attribute whether to ignore evaluation errors.
                                if (node->root->state.symbols[flake_schemas::toAttrPath(node)[0]] == "legacyPackages")
                                    j.emplace("failed", true);
                                else
                                    throw;
                            }
                            children.emplace(state->symbols[attrName], std::move(j));
                        });
                        obj.emplace("children", std::move(children));
                    },

                    [&](ref<eval_cache::AttrCursor> node, const std::vector<std::string> & systems)
                    {
                        obj.emplace("filtered", true);
                    });
            };

            auto res = nlohmann::json::object();

            flake_schemas::forEachOutput(inventory, [&](Symbol outputName, std::shared_ptr<eval_cache::AttrCursor> output, const std::string & doc, bool isLast)
            {
                auto j = nlohmann::json::object();

                if (!showLegacy && state->symbols[outputName] == "legacyPackages") {
                    j.emplace("skipped", true);
                } else if (output) {
                    j.emplace("doc", doc);
                    auto j2 = nlohmann::json::object();
                    visit(ref(output), j2);
                    j.emplace("output", std::move(j2));
                } else
                    j.emplace("unknown", true);

                res.emplace(state->symbols[outputName], j);
            });

            logger->cout("%s", res.dump());
        }

        else {
            logger->cout(ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef);

            std::function<void(
                ref<eval_cache::AttrCursor> node,
                const std::string & headerPrefix,
                const std::string & prevPrefix)> visit;

            visit = [&](
                ref<eval_cache::AttrCursor> node,
                const std::string & headerPrefix,
                const std::string & prevPrefix)
            {
                flake_schemas::visit(
                    showAllSystems ? std::optional<std::string>() : localSystem,
                    node,

                    [&](ref<eval_cache::AttrCursor> leaf)
                    {
                        auto s = headerPrefix;

                        if (auto what = flake_schemas::what(leaf))
                            s += fmt(": %s", *what);

                        if (auto drv = flake_schemas::derivation(leaf))
                            s += fmt(ANSI_ITALIC " [%s]" ANSI_NORMAL, drv->getAttr(state->sName)->getString());

                        logger->cout(s);
                    },

                    [&](std::function<void(flake_schemas::ForEachChild)> forEachChild)
                    {
                        logger->cout(headerPrefix);
                        forEachChild([&](Symbol attrName, ref<eval_cache::AttrCursor> node, bool isLast)
                        {
                            visit(node,
                                fmt(ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL, prevPrefix,
                                    isLast ? treeLast : treeConn, state->symbols[attrName]),
                                prevPrefix + (isLast ? treeNull : treeLine));
                        });
                    },

                    [&](ref<eval_cache::AttrCursor> node, const std::vector<std::string> & systems)
                    {
                        logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--all-systems' to show)", headerPrefix));
                    });
            };

            flake_schemas::forEachOutput(inventory, [&](Symbol outputName, std::shared_ptr<eval_cache::AttrCursor> output, const std::string & doc, bool isLast)
            {
                auto headerPrefix = fmt(
                    ANSI_GREEN "%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL,
                    isLast ? treeLast : treeConn, state->symbols[outputName]);

                if (!showLegacy && state->symbols[outputName] == "legacyPackages") {
                    logger->cout(headerPrefix);
                    logger->cout(
                        ANSI_GREEN "%s" "%s" ANSI_NORMAL ANSI_ITALIC "%s" ANSI_NORMAL,
                        isLast ? treeNull : treeLine,
                        treeLast,
                        "(skipped; use '--legacy' to show)");
                } else if (output) {
                    visit(ref(output), headerPrefix, isLast ? treeNull : treeLine);
                } else {
                    logger->cout(headerPrefix);
                    logger->cout(
                        ANSI_GREEN "%s" "%s" ANSI_NORMAL ANSI_ITALIC "%s" ANSI_NORMAL,
                        isLast ? treeNull : treeLine,
                        treeLast,
                        "(unknown flake output)");
                }
            });
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
            .completer = completePath
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
        auto resolvedRef = originalRef.resolve(store);
        auto [accessor, lockedRef] = resolvedRef.lazyFetch(store);
        auto storePath = fetchToStore(*store, accessor, FetchMode::Copy, lockedRef.input.getName());
        auto hash = store->queryPathInfo(storePath)->narHash;

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(HashFormat::SRI, true);
            res["original"] = fetchers::attrsToJSON(resolvedRef.toAttrs());
            res["locked"] = fetchers::attrsToJSON(lockedRef.toAttrs());
            logger->cout(res.dump());
        } else {
            notice("Downloaded '%s' to '%s' (hash '%s').",
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
        : NixMultiCommand(
            "flake",
            {
                {"update", []() { return make_ref<CmdFlakeUpdate>(); }},
                {"lock", []() { return make_ref<CmdFlakeLock>(); }},
                {"metadata", []() { return make_ref<CmdFlakeMetadata>(); }},
                {"info", []() { return make_ref<CmdFlakeInfo>(); }},
                {"check", []() { return make_ref<CmdFlakeCheck>(); }},
                {"init", []() { return make_ref<CmdFlakeInit>(); }},
                {"new", []() { return make_ref<CmdFlakeNew>(); }},
                {"clone", []() { return make_ref<CmdFlakeClone>(); }},
                {"archive", []() { return make_ref<CmdFlakeArchive>(); }},
                {"show", []() { return make_ref<CmdFlakeShow>(); }},
                {"prefetch", []() { return make_ref<CmdFlakePrefetch>(); }},
            })
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

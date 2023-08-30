#include "command.hh"
#include "installable-flake.hh"
#include "common-args.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "eval-settings.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "outputs-spec.hh"
#include "attr-path.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "eval-cache.hh"
#include "markdown.hh"

#include <nlohmann/json.hpp>
#include <queue>
#include <iomanip>

using namespace nix;
using namespace nix::flake;
using json = nlohmann::json;

class FlakeCommand : virtual Args, public MixFlakeOptions
{
    std::string flakeUrl = ".";

public:

    FlakeCommand()
    {
        expectArgs({
            .label = "flake-url",
            .optional = true,
            .handler = {&flakeUrl},
            .completer = {[&](size_t, std::string_view prefix) {
                completeFlakeRef(getStore(), prefix);
            }}
        });
    }

    FlakeRef getFlakeRef()
    {
        return parseFlakeRef(flakeUrl, absPath(".")); //FIXME
    }

    LockedFlake lockFlake()
    {
        return flake::lockFlake(*getEvalState(), getFlakeRef(), lockFlags);
    }

    std::vector<std::string> getFlakesForCompletion() override
    {
        return {flakeUrl};
    }
};

struct CmdFlakeUpdate : FlakeCommand
{
    std::string description() override
    {
        return "update flake lock file";
    }

    CmdFlakeUpdate()
    {
        /* Remove flags that don't make sense. */
        removeFlag("recreate-lock-file");
        removeFlag("update-input");
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

        lockFlags.recreateLockFile = true;
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

        if (json) {
            nlohmann::json j;
            if (flake.description)
                j["description"] = *flake.description;
            j["originalUrl"] = flake.originalRef.to_string();
            j["original"] = fetchers::attrsToJSON(flake.originalRef.toAttrs());
            j["resolvedUrl"] = flake.resolvedRef.to_string();
            j["resolved"] = fetchers::attrsToJSON(flake.resolvedRef.toAttrs());
            j["url"] = flake.lockedRef.to_string(); // FIXME: rename to lockedUrl
            j["locked"] = fetchers::attrsToJSON(flake.lockedRef.toAttrs());
            if (auto rev = flake.lockedRef.input.getRev())
                j["revision"] = rev->to_string(Base16, false);
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                j["dirtyRevision"] = *dirtyRev;
            if (auto revCount = flake.lockedRef.input.getRevCount())
                j["revCount"] = *revCount;
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                j["lastModified"] = *lastModified;
            j["path"] = store->printStorePath(flake.sourceInfo->storePath);
            j["locks"] = lockedFlake.lockFile.toJSON();
            logger->cout("%s", j.dump());
        } else {
            logger->cout(
                ANSI_BOLD "Resolved URL:" ANSI_NORMAL "  %s",
                flake.resolvedRef.to_string());
            logger->cout(
                ANSI_BOLD "Locked URL:" ANSI_NORMAL "    %s",
                flake.lockedRef.to_string());
            if (flake.description)
                logger->cout(
                    ANSI_BOLD "Description:" ANSI_NORMAL "   %s",
                    *flake.description);
            logger->cout(
                ANSI_BOLD "Path:" ANSI_NORMAL "          %s",
                store->printStorePath(flake.sourceInfo->storePath));
            if (auto rev = flake.lockedRef.input.getRev())
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    rev->to_string(Base16, false));
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

            if (!lockedFlake.lockFile.root->inputs.empty())
                logger->cout(ANSI_BOLD "Inputs:" ANSI_NORMAL);

            std::set<ref<Node>> visited;

            std::function<void(const Node & node, const std::string & prefix)> recurse;

            recurse = [&](const Node & node, const std::string & prefix)
            {
                for (const auto & [i, input] : enumerate(node.inputs)) {
                    bool last = i + 1 == node.inputs.size();

                    if (auto lockedNode = std::get_if<0>(&input.second)) {
                        logger->cout("%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s",
                            prefix + (last ? treeLast : treeConn), input.first,
                            (*lockedNode)->lockedRef);

                        bool firstVisit = visited.insert(*lockedNode).second;

                        if (firstVisit) recurse(**lockedNode, prefix + (last ? treeNull : treeLine));
                    } else if (auto follows = std::get_if<1>(&input.second)) {
                        logger->cout("%s" ANSI_BOLD "%s" ANSI_NORMAL " follows input '%s'",
                            prefix + (last ? treeLast : treeConn), input.first,
                            printInputPath(*follows));
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

/* Some helper functions for processing flake schema output. */
namespace flake_schemas {

using namespace eval_cache;

std::tuple<ref<EvalCache>, ref<eval_cache::AttrCursor>>
call(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake)
{
    auto fingerprint = lockedFlake->getFingerprint();

    std::string callFlakeSchemasNix =
        #include "call-flake-schemas.nix.gen.hh"
        ;

    auto fingerprint2 = hashString(htSHA256,
        fmt("app:%s:%s",
            hashString(htSHA256, callFlakeSchemasNix).to_string(Base16, false),
            fingerprint.to_string(Base16, false)));

    // FIXME: merge with openEvalCache().
    auto cache = make_ref<EvalCache>(
        evalSettings.useEvalCache && evalSettings.pureEval
            ? std::optional { std::cref(fingerprint2) }
            : std::nullopt,
        state,
        [&state, lockedFlake, callFlakeSchemasNix]()
        {
            auto vCallFlakeSchemas = state.allocValue();
            state.eval(state.parseExprFromString(callFlakeSchemasNix, CanonPath::root), *vCallFlakeSchemas);

            auto vFlake = state.allocValue();
            flake::callFlake(state, *lockedFlake, *vFlake);

            auto vRes = state.allocValue();
            state.callFunction(*vCallFlakeSchemas, *vFlake, *vRes, noPos);

            return vRes;
        });

    return {cache, cache->getRoot()->getAttr("inventory")};
}

void forEachOutput(
    ref<AttrCursor> inventory,
    std::function<void(Symbol outputName, std::shared_ptr<AttrCursor> output, const std::string & doc, bool isLast)> f)
{
    // FIXME: handle non-IFD outputs first.
    //evalSettings.enableImportFromDerivation.setDefault(false);

    auto outputNames = inventory->getAttrs();
    for (const auto & [i, outputName] : enumerate(outputNames)) {
        auto output = inventory->getAttr(outputName);
        try {
            auto isUnknown = (bool) output->maybeGetAttr("unknown");
            f(outputName,
                isUnknown ? std::shared_ptr<AttrCursor>() : output->getAttr("children"),
                isUnknown ? "" : output->getAttr("doc")->getString(),
                i + 1 == outputNames.size());
        } catch (Error & e) {
            e.addTrace(nullptr, "while evaluating the flake output '%s':", inventory->root->state.symbols[outputName]);
            throw;
        }
    }
}

typedef std::function<void(Symbol attrName, ref<AttrCursor> attr, bool isLast)> ForEachChild;

void visit(
    std::optional<std::string> system,
    ref<AttrCursor> node,
    std::function<void(ref<AttrCursor> leaf)> visitLeaf,
    std::function<void(std::function<void(ForEachChild)>)> visitNonLeaf,
    std::function<void(ref<AttrCursor> node, const std::vector<std::string> & systems)> visitFiltered)
{
    #if 0
    Activity act(*logger, lvlInfo, actUnknown,
        fmt("evaluating '%s'", concatStringsSep(".", attrPathS)));
    #endif

    /* Apply the system type filter. */
    if (system) {
        if (auto forSystems = node->maybeGetAttr("forSystems")) {
            auto systems = forSystems->getListOfStrings();
            if (std::find(systems.begin(), systems.end(), system) == systems.end()) {
                visitFiltered(node, systems);
                return;
            }
        }
    }

    if (auto children = node->maybeGetAttr("children")) {
        visitNonLeaf([&](ForEachChild f) {
            auto attrNames = children->getAttrs();
            for (const auto & [i, attrName] : enumerate(attrNames)) {
                try {
                    f(attrName, children->getAttr(attrName), i + 1 == attrNames.size());
                } catch (Error & e) {
                    // FIXME: make it a flake schema attribute whether to ignore evaluation errors.
                    //if (!(attrPath.size() > 0 && attrPathS[0] == "legacyPackages")) {
                    if (true) {
                        e.addTrace(nullptr, "while evaluating the flake output attribute '%s':",
                            node->root->state.symbols[attrName]);
                        //concatStringsSep(".", attrName));
                        throw;
                    }
                }
            }
        });
    }

    else
        visitLeaf(ref(node));
}

std::optional<std::string> what(ref<AttrCursor> leaf)
{
    if (auto what = leaf->maybeGetAttr("what"))
        return what->getString();
    else
        return std::nullopt;
}

std::optional<std::string> shortDescription(ref<AttrCursor> leaf)
{
    if (auto what = leaf->maybeGetAttr("shortDescription")) {
        auto s = trim(what->getString());
        if (s != "") return s;
    }
    return std::nullopt;
}

std::shared_ptr<AttrCursor> derivation(ref<AttrCursor> leaf)
{
    return leaf->maybeGetAttr("derivation");
}

}

struct CmdFlakeCheck : FlakeCommand
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

        auto [cache, inventory] = flake_schemas::call(*state, flake);

        std::vector<DerivedPath> drvPaths;

        std::set<std::string> uncheckedOutputs;
        std::set<std::string> omittedSystems;

        std::function<void(ref<eval_cache::AttrCursor> node)> visit;

        bool hasErrors = false;

        // FIXME: keep-going
        #if 0
        auto reportError = [&](const Error & e) {
            try {
                throw e;
            } catch (Error & e) {
                if (settings.keepGoing) {
                    ignoreException();
                    hasErrors = true;
                }
                else
                    throw;
            }
        };
        #endif

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
                            auto b = evalChecks->getAttr(checkName)->getBool();
                            if (!b)
                                // FIXME: show full attrpath
                                warn("Evaluation check '%s' failed.", state->symbols[checkName]);
                        }
                    }

                    if (auto isFlakeCheck = leaf->maybeGetAttr("isFlakeCheck")) {
                        if (isFlakeCheck->getBool()) {
                            auto drvPath = leaf->getAttr("derivation")->forceDerivation();
                            drvPaths.push_back(DerivedPath::Built {
                                .drvPath = makeConstantStorePathRef(drvPath),
                                .outputs = OutputsSpec::All { },
                            });
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
                uncheckedOutputs.insert(state->symbols[outputName]);
        });

        if (!uncheckedOutputs.empty())
            warn("The following flake outputs are unchecked: %s.",
                concatStringsSep(", ", uncheckedOutputs)); // FIXME: quote

        if (build && !drvPaths.empty()) {
            Activity act(*logger, lvlInfo, actUnknown, "running flake checks");
            store->buildPaths(drvPaths);
        }

        if (hasErrors)
            throw Error("some errors were encountered during the evaluation");

        if (!omittedSystems.empty()) {
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
            .completer = {[&](size_t, std::string_view prefix) {
                completeFlakeRefWithFragment(
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

        auto [templateFlakeRef, templateName] = parseFlakeRefWithFragment(templateUrl, absPath("."));

        auto installable = InstallableFlake(nullptr,
            evalState, std::move(templateFlakeRef), templateName, DefaultOutputs(),
            defaultTemplateAttrPaths,
            defaultTemplateAttrPathsPrefixes,
            lockFlags);

        auto cursor = installable.getCursor(*evalState);

        auto templateDirAttr = cursor->getAttr("path");
        auto templateDir = templateDirAttr->getString();

        if (!store->isInStore(templateDir))
            throw TypeError(
                "'%s' was not found in the Nix store\n"
                "If you've set '%s' to a string, try using a path instead.",
                templateDir, templateDirAttr->getAttrPathStr());

        std::vector<Path> changedFiles;
        std::vector<Path> conflictedFiles;

        std::function<void(const Path & from, const Path & to)> copyDir;
        copyDir = [&](const Path & from, const Path & to)
        {
            createDirs(to);

            for (auto & entry : readDirectory(from)) {
                auto from2 = from + "/" + entry.name;
                auto to2 = to + "/" + entry.name;
                auto st = lstat(from2);
                if (S_ISDIR(st.st_mode))
                    copyDir(from2, to2);
                else if (S_ISREG(st.st_mode)) {
                    auto contents = readFile(from2);
                    if (pathExists(to2)) {
                        auto contents2 = readFile(to2);
                        if (contents != contents2) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2, from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        writeFile(to2, contents);
                }
                else if (S_ISLNK(st.st_mode)) {
                    auto target = readLink(from2);
                    if (pathExists(to2)) {
                        if (readLink(to2) != target) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2, from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                          createSymlink(target, to2);
                }
                else
                    throw Error("file '%s' has unsupported type", from2);
                changedFiles.push_back(to2);
                notice("wrote: %s", to2);
            }
        };

        copyDir(templateDir, flakeDir);

        if (!changedFiles.empty() && pathExists(flakeDir + "/.git")) {
            Strings args = { "-C", flakeDir, "add", "--intent-to-add", "--force", "--" };
            for (auto & s : changedFiles) args.push_back(s);
            runProgram("git", true, args);
        }
        auto welcomeText = cursor->maybeGetAttr("welcomeText");
        if (welcomeText) {
            notice("\n");
            notice(renderMarkdownToTerminal(welcomeText->getString()));
        }

        if (!conflictedFiles.empty())
            throw Error("Encountered %d conflicts - see above", conflictedFiles.size());
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

        sources.insert(flake.flake.sourceInfo->storePath);

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
                        : (*inputNode)->lockedRef.input.fetch(store).first.storePath;
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
                {"path", store->printStorePath(flake.flake.sourceInfo->storePath)},
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

struct CmdFlakeShow : FlakeCommand, MixJSON
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

        auto [cache, inventory] = flake_schemas::call(*state, flake);

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
                            obj.emplace("what", what);

                        if (auto shortDescription = flake_schemas::shortDescription(leaf))
                            obj.emplace("shortDescription", shortDescription);

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
                            visit(node, j);
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

                if (output) {
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

                if (output) {
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
    CmdFlakePrefetch()
    {
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
        auto [tree, lockedRef] = resolvedRef.fetchTree(store);
        auto hash = store->queryPathInfo(tree.storePath)->narHash;

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(tree.storePath);
            res["hash"] = hash.to_string(SRI, true);
            logger->cout(res.dump());
        } else {
            notice("Downloaded '%s' to '%s' (hash '%s').",
                lockedRef.to_string(),
                store->printStorePath(tree.storePath),
                hash.to_string(SRI, true));
        }
    }
};

struct CmdFlake : NixMultiCommand
{
    CmdFlake()
        : MultiCommand({
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
        if (!command)
            throw UsageError("'nix flake' requires a sub-command.");
        experimentalFeatureSettings.require(Xp::Flakes);
        command->second->run();
    }
};

static auto rCmdFlake = registerCommand<CmdFlake>("flake");

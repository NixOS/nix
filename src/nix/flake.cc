#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "attr-path.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "json.hh"
#include "sqlite.hh"

#include <nlohmann/json.hpp>
#include <queue>
#include <iomanip>

using namespace nix;
using namespace nix::flake;

class FlakeCommand : virtual Args, public EvalCommand, public MixFlakeOptions
{
    std::string flakeUrl = ".";

public:

    FlakeCommand()
    {
        expectArg("flake-url", &flakeUrl, true);
    }

    FlakeRef getFlakeRef()
    {
        return parseFlakeRef(flakeUrl, absPath(".")); //FIXME
    }

    Flake getFlake()
    {
        auto evalState = getEvalState();
        return flake::getFlake(*evalState, getFlakeRef(), lockFlags.useRegistries);
    }

    LockedFlake lockFlake()
    {
        return flake::lockFlake(*getEvalState(), getFlakeRef(), lockFlags);
    }
};

struct CmdFlakeList : EvalCommand
{
    std::string description() override
    {
        return "list available Nix flakes";
    }

    void run(nix::ref<nix::Store> store) override
    {
        using namespace fetchers;

        auto registries = getRegistries(store);

        for (auto & registry : registries) {
            for (auto & entry : registry->entries) {
                // FIXME: format nicely
                logger->stdout("%s %s %s",
                    registry->type == Registry::Flag   ? "flags " :
                    registry->type == Registry::User   ? "user  " :
                    registry->type == Registry::System ? "system" :
                    "global",
                    entry.from->to_string(),
                    entry.to->to_string());
            }
        }
    }
};

static void printFlakeInfo(const Store & store, const Flake & flake)
{
    logger->stdout("Resolved URL:  %s", flake.resolvedRef.to_string());
    logger->stdout("Locked URL:    %s", flake.lockedRef.to_string());
    if (flake.description)
        logger->stdout("Description:   %s", *flake.description);
    logger->stdout("Path:          %s", store.printStorePath(flake.sourceInfo->storePath));
    if (auto rev = flake.lockedRef.input->getRev())
        logger->stdout("Revision:      %s", rev->to_string(Base16, false));
    if (flake.sourceInfo->info.revCount)
        logger->stdout("Revisions:     %s", *flake.sourceInfo->info.revCount);
    if (flake.sourceInfo->info.lastModified)
        logger->stdout("Last modified: %s",
            std::put_time(std::localtime(&*flake.sourceInfo->info.lastModified), "%F %T"));
}

static nlohmann::json flakeToJson(const Store & store, const Flake & flake)
{
    nlohmann::json j;
    if (flake.description)
        j["description"] = *flake.description;
    j["originalUrl"] = flake.originalRef.to_string();
    j["original"] = attrsToJson(flake.originalRef.toAttrs());
    j["resolvedUrl"] = flake.resolvedRef.to_string();
    j["resolved"] = attrsToJson(flake.resolvedRef.toAttrs());
    j["url"] = flake.lockedRef.to_string(); // FIXME: rename to lockedUrl
    j["locked"] = attrsToJson(flake.lockedRef.toAttrs());
    j["info"] = flake.sourceInfo->info.toJson();
    if (auto rev = flake.lockedRef.input->getRev())
        j["revision"] = rev->to_string(Base16, false);
    if (flake.sourceInfo->info.revCount)
        j["revCount"] = *flake.sourceInfo->info.revCount;
    if (flake.sourceInfo->info.lastModified)
        j["lastModified"] = *flake.sourceInfo->info.lastModified;
    j["path"] = store.printStorePath(flake.sourceInfo->storePath);
    return j;
}

struct CmdFlakeUpdate : FlakeCommand
{
    std::string description() override
    {
        return "update flake lock file";
    }

    void run(nix::ref<nix::Store> store) override
    {
        /* Use --refresh by default for 'nix flake update'. */
        settings.tarballTtl = 0;

        lockFlake();
    }
};

static void enumerateOutputs(EvalState & state, Value & vFlake,
    std::function<void(const std::string & name, Value & vProvide, const Pos & pos)> callback)
{
    state.forceAttrs(vFlake);

    auto aOutputs = vFlake.attrs->get(state.symbols.create("outputs"));
    assert(aOutputs);

    state.forceAttrs(*aOutputs->value);

    for (auto & attr : *aOutputs->value->attrs)
        callback(attr.name, *attr.value, *attr.pos);
}

struct CmdFlakeInfo : FlakeCommand, MixJSON
{
    std::string description() override
    {
        return "list info about a given flake";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = getFlake();

        if (json) {
            auto json = flakeToJson(*store, flake);
            logger->stdout("%s", json.dump());
        } else
            printFlakeInfo(*store, flake);
    }
};

struct CmdFlakeListInputs : FlakeCommand, MixJSON
{
    std::string description() override
    {
        return "list flake inputs";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = lockFlake();

        if (json)
            logger->stdout("%s", flake.lockFile.toJson());
        else {
            logger->stdout("%s", flake.flake.lockedRef);

            std::function<void(const Node & node, const std::string & prefix)> recurse;

            recurse = [&](const Node & node, const std::string & prefix)
            {
                for (const auto & [i, input] : enumerate(node.inputs)) {
                    //auto tree2 = tree.child(i + 1 == inputs.inputs.size());
                    bool last = i + 1 == node.inputs.size();
                    logger->stdout("%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s",
                        prefix + (last ? treeLast : treeConn), input.first,
                        std::dynamic_pointer_cast<const LockedNode>(input.second)->lockedRef);
                    recurse(*input.second, prefix + (last ? treeNull : treeLine));
                }
            };

            recurse(*flake.lockFile.root, "");
        }
    }
};

struct CmdFlakeCheck : FlakeCommand
{
    bool build = true;

    CmdFlakeCheck()
    {
        mkFlag()
            .longName("no-build")
            .description("do not build checks")
            .set(&build, false);
    }

    std::string description() override
    {
        return "check whether the flake evaluates and run its tests";
    }

    void run(nix::ref<nix::Store> store) override
    {
        settings.readOnlyMode = !build;

        auto state = getEvalState();
        auto flake = lockFlake();

        auto checkSystemName = [&](const std::string & system, const Pos & pos) {
            // FIXME: what's the format of "system"?
            if (system.find('-') == std::string::npos)
                throw Error("'%s' is not a valid system type, at %s", system, pos);
        };

        auto checkDerivation = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                auto drvInfo = getDerivation(*state, v, false);
                if (!drvInfo)
                    throw Error("flake attribute '%s' is not a derivation", attrPath);
                // FIXME: check meta attributes
                return store->parseStorePath(drvInfo->queryDrvPath());
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the derivation '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        std::vector<StorePathWithOutputs> drvPaths;

        auto checkApp = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                auto app = App(*state, v);
                for (auto & i : app.context) {
                    auto [drvPathS, outputName] = decodeContext(i);
                    auto drvPath = store->parseStorePath(drvPathS);
                    if (!outputName.empty() && drvPath.isDerivation())
                        drvPaths.emplace_back(drvPath);
                }
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the app definition '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        auto checkOverlay = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                state->forceValue(v, pos);
                if (v.type != tLambda || v.lambda.fun->matchAttrs || std::string(v.lambda.fun->arg) != "final")
                    throw Error("overlay does not take an argument named 'final'");
                auto body = dynamic_cast<ExprLambda *>(v.lambda.fun->body);
                if (!body || body->matchAttrs || std::string(body->arg) != "prev")
                    throw Error("overlay does not take an argument named 'prev'");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // evaluate the overlay.
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the overlay '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        auto checkModule = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                state->forceValue(v, pos);
                if (v.type == tLambda) {
                    if (!v.lambda.fun->matchAttrs || !v.lambda.fun->formals->ellipsis)
                        throw Error("module must match an open attribute set ('{ config, ... }')");
                } else if (v.type == tAttrs) {
                    for (auto & attr : *v.attrs)
                        try {
                            state->forceValue(*attr.value, *attr.pos);
                        } catch (Error & e) {
                            e.addPrefix(fmt("while evaluating the option '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attr.name, *attr.pos));
                            throw;
                        }
                } else
                    throw Error("module must be a function or an attribute set");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // check the module.
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the NixOS module '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        std::function<void(const std::string & attrPath, Value & v, const Pos & pos)> checkHydraJobs;

        checkHydraJobs = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                state->forceAttrs(v, pos);

                if (state->isDerivation(v))
                    throw Error("jobset should not be a derivation at top-level");

                for (auto & attr : *v.attrs) {
                    state->forceAttrs(*attr.value, *attr.pos);
                    if (!state->isDerivation(*attr.value))
                        checkHydraJobs(attrPath + "." + (std::string) attr.name,
                            *attr.value, *attr.pos);
                }

            } catch (Error & e) {
                e.addPrefix(fmt("while checking the Hydra jobset '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        auto checkNixOSConfiguration = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                Activity act(*logger, lvlChatty, actUnknown,
                    fmt("checking NixOS configuration '%s'", attrPath));
                Bindings & bindings(*state->allocBindings(0));
                auto vToplevel = findAlongAttrPath(*state, "config.system.build.toplevel", bindings, v).first;
                state->forceAttrs(*vToplevel, pos);
                if (!state->isDerivation(*vToplevel))
                    throw Error("attribute 'config.system.build.toplevel' is not a derivation");
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the NixOS configuration '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        {
            Activity act(*logger, lvlInfo, actUnknown, "evaluating flake");

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            enumerateOutputs(*state,
                *vFlake,
                [&](const std::string & name, Value & vOutput, const Pos & pos) {
                    Activity act(*logger, lvlChatty, actUnknown,
                        fmt("checking flake output '%s'", name));

                    try {
                        state->forceValue(vOutput, pos);

                        if (name == "checks") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(attr.name, *attr.pos);
                                state->forceAttrs(*attr.value, *attr.pos);
                                for (auto & attr2 : *attr.value->attrs) {
                                    auto drvPath = checkDerivation(
                                        fmt("%s.%s.%s", name, attr.name, attr2.name),
                                        *attr2.value, *attr2.pos);
                                    if ((std::string) attr.name == settings.thisSystem.get())
                                        drvPaths.emplace_back(drvPath);
                                }
                            }
                        }

                        else if (name == "packages") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(attr.name, *attr.pos);
                                state->forceAttrs(*attr.value, *attr.pos);
                                for (auto & attr2 : *attr.value->attrs)
                                    checkDerivation(
                                        fmt("%s.%s.%s", name, attr.name, attr2.name),
                                        *attr2.value, *attr2.pos);
                            }
                        }

                        else if (name == "apps") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(attr.name, *attr.pos);
                                state->forceAttrs(*attr.value, *attr.pos);
                                for (auto & attr2 : *attr.value->attrs)
                                    checkApp(
                                        fmt("%s.%s.%s", name, attr.name, attr2.name),
                                        *attr2.value, *attr2.pos);
                            }
                        }

                        else if (name == "defaultPackage" || name == "devShell") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(attr.name, *attr.pos);
                                checkDerivation(
                                    fmt("%s.%s", name, attr.name),
                                    *attr.value, *attr.pos);
                            }
                        }

                        else if (name == "defaultApp") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(attr.name, *attr.pos);
                                checkApp(
                                    fmt("%s.%s", name, attr.name),
                                    *attr.value, *attr.pos);
                            }
                        }

                        else if (name == "legacyPackages") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(attr.name, *attr.pos);
                                // FIXME: do getDerivations?
                            }
                        }

                        else if (name == "overlay")
                            checkOverlay(name, vOutput, pos);

                        else if (name == "overlays") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkOverlay(fmt("%s.%s", name, attr.name),
                                    *attr.value, *attr.pos);
                        }

                        else if (name == "nixosModule")
                            checkModule(name, vOutput, pos);

                        else if (name == "nixosModules") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkModule(fmt("%s.%s", name, attr.name),
                                    *attr.value, *attr.pos);
                        }

                        else if (name == "nixosConfigurations") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkNixOSConfiguration(fmt("%s.%s", name, attr.name),
                                    *attr.value, *attr.pos);
                        }

                        else if (name == "hydraJobs")
                            checkHydraJobs(name, vOutput, pos);

                        else
                            warn("unknown flake output '%s'", name);

                    } catch (Error & e) {
                        e.addPrefix(fmt("while checking flake output '" ANSI_BOLD "%s" ANSI_NORMAL "':\n", name));
                        throw;
                    }
                });
        }

        if (build && !drvPaths.empty()) {
            Activity act(*logger, lvlInfo, actUnknown, "running flake checks");
            store->buildPaths(drvPaths);
        }
    }
};

struct CmdFlakeAdd : MixEvalArgs, Command
{
    std::string fromUrl, toUrl;

    std::string description() override
    {
        return "upsert flake in user flake registry";
    }

    CmdFlakeAdd()
    {
        expectArg("from-url", &fromUrl);
        expectArg("to-url", &toUrl);
    }

    void run() override
    {
        auto fromRef = parseFlakeRef(fromUrl);
        auto toRef = parseFlakeRef(toUrl);
        fetchers::Attrs extraAttrs;
        if (toRef.subdir != "") extraAttrs["dir"] = toRef.subdir;
        auto userRegistry = fetchers::getUserRegistry();
        userRegistry->remove(fromRef.input);
        userRegistry->add(fromRef.input, toRef.input, extraAttrs);
        userRegistry->write(fetchers::getUserRegistryPath());
    }
};

struct CmdFlakeRemove : virtual Args, MixEvalArgs, Command
{
    std::string url;

    std::string description() override
    {
        return "remove flake from user flake registry";
    }

    CmdFlakeRemove()
    {
        expectArg("url", &url);
    }

    void run() override
    {
        auto userRegistry = fetchers::getUserRegistry();
        userRegistry->remove(parseFlakeRef(url).input);
        userRegistry->write(fetchers::getUserRegistryPath());
    }
};

struct CmdFlakePin : virtual Args, EvalCommand
{
    std::string url;

    std::string description() override
    {
        return "pin a flake to its current version in user flake registry";
    }

    CmdFlakePin()
    {
        expectArg("url", &url);
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto ref = parseFlakeRef(url);
        auto userRegistry = fetchers::getUserRegistry();
        userRegistry->remove(ref.input);
        auto [tree, resolved] = ref.resolve(store).input->fetchTree(store);
        fetchers::Attrs extraAttrs;
        if (ref.subdir != "") extraAttrs["dir"] = ref.subdir;
        userRegistry->add(ref.input, resolved, extraAttrs);
    }
};

struct CmdFlakeInit : virtual Args, Command
{
    std::string description() override
    {
        return "create a skeleton 'flake.nix' file in the current directory";
    }

    void run() override
    {
        Path flakeDir = absPath(".");

        if (!pathExists(flakeDir + "/.git"))
            throw Error("the directory '%s' is not a Git repository", flakeDir);

        Path flakePath = flakeDir + "/flake.nix";

        if (pathExists(flakePath))
            throw Error("file '%s' already exists", flakePath);

        writeFile(flakePath,
#include "flake-template.nix.gen.hh"
            );
    }
};

struct CmdFlakeClone : FlakeCommand
{
    Path destDir;

    std::string description() override
    {
        return "clone flake repository";
    }

    CmdFlakeClone()
    {
        mkFlag()
            .shortName('f')
            .longName("dest")
            .label("path")
            .description("destination path")
            .dest(&destDir);
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (destDir.empty())
            throw Error("missing flag '--dest'");

        getFlakeRef().resolve(store).input->clone(destDir);
    }
};

struct CmdFlakeArchive : FlakeCommand, MixJSON, MixDryRun
{
    std::string dstUri;

    CmdFlakeArchive()
    {
        mkFlag()
            .longName("to")
            .labels({"store-uri"})
            .description("URI of the destination Nix store")
            .dest(&dstUri);
    }

    std::string description() override
    {
        return "copy a flake and all its inputs to a store";
    }

    Examples examples() override
    {
        return {
            Example{
                "To copy the dwarffs flake and its dependencies to a binary cache:",
                "nix flake archive --to file:///tmp/my-cache dwarffs"
            },
            Example{
                "To fetch the dwarffs flake and its dependencies to the local Nix store:",
                "nix flake archive dwarffs"
            },
            Example{
                "To print the store paths of the flake sources of NixOps without fetching them:",
                "nix flake archive --json --dry-run nixops"
            },
        };
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = lockFlake();

        auto jsonRoot = json ? std::optional<JSONObject>(std::cout) : std::nullopt;

        StorePathSet sources;

        sources.insert(flake.flake.sourceInfo->storePath.clone());
        if (jsonRoot)
            jsonRoot->attr("path", store->printStorePath(flake.flake.sourceInfo->storePath));

        // FIXME: use graph output, handle cycles.
        std::function<void(const Node & node, std::optional<JSONObject> & jsonObj)> traverse;
        traverse = [&](const Node & node, std::optional<JSONObject> & jsonObj)
        {
            auto jsonObj2 = jsonObj ? jsonObj->object("inputs") : std::optional<JSONObject>();
            for (auto & input : node.inputs) {
                auto lockedInput = std::dynamic_pointer_cast<const LockedNode>(input.second);
                assert(lockedInput);
                auto jsonObj3 = jsonObj2 ? jsonObj2->object(input.first) : std::optional<JSONObject>();
                if (!dryRun)
                    lockedInput->lockedRef.input->fetchTree(store);
                auto storePath = lockedInput->computeStorePath(*store);
                if (jsonObj3)
                    jsonObj3->attr("path", store->printStorePath(storePath));
                sources.insert(std::move(storePath));
                traverse(*lockedInput, jsonObj3);
            }
        };

        traverse(*flake.lockFile.root, jsonRoot);

        if (!dryRun && !dstUri.empty()) {
            ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);
            copyPaths(store, dstStore, sources);
        }
    }
};

// FIXME: inefficient representation of attrs
static const char * schema = R"sql(
create table if not exists Attributes (
    parent      integer not null,
    name        text,
    type        integer not null,
    value       text,
    primary key (parent, name)
);
)sql";

enum AttrType {
    Placeholder = 0,
    // FIXME: distinguish between full / partial attrsets
    Attrs = 1,
    String = 2,
};

struct AttrDb
{
    struct State
    {
        SQLite db;
        SQLiteStmt insertAttribute;
        SQLiteStmt queryAttribute;
        SQLiteStmt queryAttributes;
    };

    struct placeholder_t {};
    typedef uint64_t AttrId;
    typedef std::pair<AttrId, Symbol> AttrKey;
    typedef std::variant<std::vector<Symbol>, std::string, placeholder_t> AttrValue;

    std::unique_ptr<Sync<State>> _state;

    AttrDb(const Fingerprint & fingerprint)
        : _state(std::make_unique<Sync<State>>())
    {
        auto state(_state->lock());

        Path cacheDir = getCacheDir() + "/nix/eval-cache-v2";
        createDirs(cacheDir);

        Path dbPath = cacheDir + "/" + fingerprint.to_string(Base16, false) + ".sqlite";

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema);

        state->insertAttribute.create(state->db,
            "insert or replace into Attributes(parent, name, type, value) values (?, ?, ?, ?)");

        state->queryAttribute.create(state->db,
            "select rowid, type, value from Attributes where parent = ? and name = ?");

        state->queryAttributes.create(state->db,
            "select name from Attributes where parent = ?");
    }

    AttrId setAttr(
        AttrKey key,
        const std::vector<Symbol> & attrs)
    {
        auto state(_state->lock());

        state->insertAttribute.use()
            (key.first)
            (key.second)
            (AttrType::Attrs)
            (0, false).exec();

        AttrId rowId = state->db.getLastInsertedRowId();
        assert(rowId);

        for (auto & attr : attrs)
            state->insertAttribute.use()
                (rowId)
                (attr)
                (AttrType::Placeholder)
                (0, false).exec();

        return rowId;
    }

    AttrId setAttr(
        AttrKey key,
        std::string_view s)
    {
        auto state(_state->lock());

        state->insertAttribute.use()
            (key.first)
            (key.second)
            (AttrType::String)
            (s).exec();

        return state->db.getLastInsertedRowId();
    }

    std::optional<std::pair<AttrId, AttrValue>> getAttr(
        AttrKey key,
        SymbolTable & symbols)
    {
        auto state(_state->lock());

        auto queryAttribute(state->queryAttribute.use()(key.first)(key.second));
        if (!queryAttribute.next()) return {};

        auto rowId = (AttrType) queryAttribute.getInt(0);
        auto type = (AttrType) queryAttribute.getInt(1);

        if (type == AttrType::Placeholder)
            return {{rowId, placeholder_t()}};
        else if (type == AttrType::Attrs) {
            // FIXME: expensive, should separate this out.
            std::vector<Symbol> attrs;
            auto queryAttributes(state->queryAttributes.use()(rowId));
            while (queryAttributes.next())
                attrs.push_back(symbols.create(queryAttributes.getStr(0)));
            return {{rowId, attrs}};
        } else if (type == AttrType::String) {
            return {{rowId, queryAttribute.getStr(2)}};
        } else
            throw Error("unexpected type in evaluation cache");
    }
};

struct AttrCursor;

struct AttrRoot : std::enable_shared_from_this<AttrRoot>
{
    std::shared_ptr<AttrDb> db;
    EvalState & state;
    typedef std::function<Value *()> RootLoader;
    RootLoader rootLoader;
    RootValue value;

    AttrRoot(std::shared_ptr<AttrDb> db, EvalState & state, RootLoader rootLoader)
        : db(db)
        , state(state)
        , rootLoader(rootLoader)
    {
    }

    Value * getRootValue()
    {
        if (!value) {
            //printError("GET ROOT");
            value = allocRootValue(rootLoader());
        }
        return *value;
    }

    std::shared_ptr<AttrCursor> getRoot()
    {
        return std::make_shared<AttrCursor>(ref(shared_from_this()), std::nullopt);
    }
};

struct AttrCursor : std::enable_shared_from_this<AttrCursor>
{
    ref<AttrRoot> root;
    typedef std::optional<std::pair<std::shared_ptr<AttrCursor>, Symbol>> Parent;
    Parent parent;
    RootValue _value;
    std::optional<std::pair<AttrDb::AttrId, AttrDb::AttrValue>> cachedValue;

    AttrCursor(
        ref<AttrRoot> root,
        Parent parent,
        Value * value = nullptr,
        std::optional<std::pair<AttrDb::AttrId, AttrDb::AttrValue>> && cachedValue = {})
        : root(root), parent(parent), cachedValue(std::move(cachedValue))
    {
        if (value)
            _value = allocRootValue(value);
    }

    AttrDb::AttrKey getAttrKey()
    {
        if (!parent)
            return {0, root->state.sEpsilon};
        if (!parent->first->cachedValue) {
            parent->first->cachedValue = root->db->getAttr(
                parent->first->getAttrKey(), root->state.symbols);
            assert(parent->first->cachedValue);
        }
        return {parent->first->cachedValue->first, parent->second};
    }

    Value & getValue()
    {
        if (!_value) {
            if (parent) {
                auto & vParent = parent->first->getValue();
                root->state.forceAttrs(vParent);
                auto attr = vParent.attrs->get(parent->second);
                if (!attr)
                    throw Error("attribute '%s' is unexpectedly missing", getAttrPathStr());
                _value = allocRootValue(attr->value);
            } else
                _value = allocRootValue(root->getRootValue());
        }
        return **_value;
    }

    std::vector<Symbol> getAttrPath() const
    {
        if (parent) {
            auto attrPath = parent->first->getAttrPath();
            attrPath.push_back(parent->second);
            return attrPath;
        } else
            return {};
    }

    std::vector<Symbol> getAttrPath(Symbol name) const
    {
        auto attrPath = getAttrPath();
        attrPath.push_back(name);
        return attrPath;
    }

    std::string getAttrPathStr() const
    {
        return concatStringsSep(".", getAttrPath());
    }

    std::string getAttrPathStr(Symbol name) const
    {
        return concatStringsSep(".", getAttrPath(name));
    }

    std::shared_ptr<AttrCursor> maybeGetAttr(Symbol name)
    {
        if (root->db) {
            if (!cachedValue)
                cachedValue = root->db->getAttr(getAttrKey(), root->state.symbols);

            if (cachedValue) {
                if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                    for (auto & attr : *attrs)
                        if (attr == name)
                            return std::make_shared<AttrCursor>(root, std::make_pair(shared_from_this(), name));
                    return nullptr;
                } else if (std::get_if<AttrDb::placeholder_t>(&cachedValue->second)) {
                    auto attr = root->db->getAttr({cachedValue->first, name}, root->state.symbols);
                    if (attr)
                        return std::make_shared<AttrCursor>(root, std::make_pair(shared_from_this(), name), nullptr, std::move(attr));
                    // Incomplete attrset, so need to fall thru and
                    // evaluate to see whether 'name' exists
                } else
                    // FIXME: throw error?
                    return nullptr;
            }
        }

        //printError("GET ATTR %s", getAttrPathStr(name));

        root->state.forceValue(getValue());

        if (getValue().type != tAttrs)
            return nullptr;

        auto attr = getValue().attrs->get(name);

        if (!attr)
            return nullptr;

        return std::make_shared<AttrCursor>(root, std::make_pair(shared_from_this(), name), attr->value);
    }

    std::shared_ptr<AttrCursor> maybeGetAttr(std::string_view name)
    {
        return maybeGetAttr(root->state.symbols.create(name));
    }

    std::shared_ptr<AttrCursor> getAttr(Symbol name)
    {
        auto p = maybeGetAttr(name);
        if (!p)
            throw Error("attribute '%s' does not exist", getAttrPathStr(name));
        return p;
    }

    std::shared_ptr<AttrCursor> getAttr(std::string_view name)
    {
        return getAttr(root->state.symbols.create(name));
    }

    std::string getString()
    {
        if (root->db) {
            if (!cachedValue)
                cachedValue = root->db->getAttr(getAttrKey(), root->state.symbols);
            if (cachedValue && !std::get_if<AttrDb::placeholder_t>(&cachedValue->second)) {
                if (auto s = std::get_if<std::string>(&cachedValue->second)) {
                    //printError("GOT STRING %s", getAttrPathStr());
                    return *s;
                } else
                    throw Error("unexpected type mismatch in evaluation cache (string expected)");
            }
        }

        //printError("GET STRING %s", getAttrPathStr());
        auto s = root->state.forceString(getValue());
        if (root->db)
            cachedValue = {root->db->setAttr(getAttrKey(), s), s};

        return s;
    }

    std::vector<Symbol> getAttrs()
    {
        if (root->db) {
            if (!cachedValue)
                cachedValue = root->db->getAttr(getAttrKey(), root->state.symbols);
            if (cachedValue && !std::get_if<AttrDb::placeholder_t>(&cachedValue->second)) {
                if (auto attrs = std::get_if<std::vector<Symbol>>(&cachedValue->second)) {
                    //printError("GOT ATTRS %s", getAttrPathStr());
                    return *attrs;
                } else
                    throw Error("unexpected type mismatch in evaluation cache (attrs expected)");
            }
        }

        //printError("GET ATTRS %s", getAttrPathStr());
        std::vector<Symbol> attrs;
        root->state.forceAttrs(getValue());
        for (auto & attr : *getValue().attrs)
            attrs.push_back(attr.name);
        std::sort(attrs.begin(), attrs.end(), [](const Symbol & a, const Symbol & b) {
            return (const string &) a < (const string &) b;
        });
        if (root->db)
            cachedValue = {root->db->setAttr(getAttrKey(), attrs), attrs};

        return attrs;
    }

    bool isDerivation()
    {
        auto aType = maybeGetAttr("type");
        return aType && aType->getString() == "derivation";
    }
};

struct CmdFlakeShow : FlakeCommand
{
    bool showLegacy = false;
    bool useEvalCache = true;

    CmdFlakeShow()
    {
        mkFlag()
            .longName("legacy")
            .description("show the contents of the 'legacyPackages' output")
            .set(&showLegacy, true);

        mkFlag()
            .longName("no-eval-cache")
            .description("do not use the flake evaluation cache")
            .handler([&]() { useEvalCache = false; });
    }

    std::string description() override
    {
        return "show the outputs provided by a flake";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto state = getEvalState();
        auto flake = lockFlake();

        std::function<void(AttrCursor & visitor, const std::vector<Symbol> & attrPath, const std::string & headerPrefix, const std::string & nextPrefix)> visit;

        visit = [&](AttrCursor & visitor, const std::vector<Symbol> & attrPath, const std::string & headerPrefix, const std::string & nextPrefix)
        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));
            try {
                auto recurse = [&]()
                {
                    logger->stdout("%s", headerPrefix);
                    auto attrs = visitor.getAttrs();
                    for (const auto & [i, attr] : enumerate(attrs)) {
                        bool last = i + 1 == attrs.size();
                        auto visitor2 = visitor.getAttr(attr);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        visit(*visitor2, attrPath2,
                            fmt(ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL, nextPrefix, last ? treeLast : treeConn, attr),
                            nextPrefix + (last ? treeNull : treeLine));
                    }
                };

                auto showDerivation = [&]()
                {
                    auto name = visitor.getAttr(state->sName)->getString();

                    /*
                    std::string description;

                    if (auto aMeta = visitor.maybeGetAttr("meta")) {
                        if (auto aDescription = aMeta->maybeGetAttr("description"))
                            description = aDescription->getString();
                    }
                    */

                    logger->stdout("%s: %s '%s'",
                        headerPrefix,
                        attrPath.size() == 2 && attrPath[0] == "devShell" ? "development environment" :
                        attrPath.size() == 3 && attrPath[0] == "checks" ? "derivation" :
                        attrPath.size() >= 1 && attrPath[0] == "hydraJobs" ? "derivation" :
                        "package",
                        name);
                };

                if (attrPath.size() == 0
                    || (attrPath.size() == 1 && (
                            attrPath[0] == "defaultPackage"
                            || attrPath[0] == "devShell"
                            || attrPath[0] == "nixosConfigurations"
                            || attrPath[0] == "nixosModules"
                            || attrPath[0] == "defaultApp"))
                    || ((attrPath.size() == 1 || attrPath.size() == 2)
                        && (attrPath[0] == "checks"
                            || attrPath[0] == "packages"
                            || attrPath[0] == "apps"))
                    )
                {
                    recurse();
                }

                else if (
                    (attrPath.size() == 2 && (attrPath[0] == "defaultPackage" || attrPath[0] == "devShell"))
                    || (attrPath.size() == 3 && (attrPath[0] == "checks" || attrPath[0] == "packages"))
                    )
                {
                    if (visitor.isDerivation())
                        showDerivation();
                    else
                        throw Error("expected a derivation");
                }

                else if (attrPath.size() > 0 && attrPath[0] == "hydraJobs") {
                    if (visitor.isDerivation())
                        showDerivation();
                    else
                        recurse();
                }

                else if (attrPath.size() > 0 && attrPath[0] == "legacyPackages") {
                    if (attrPath.size() == 1)
                        recurse();
                    else if (!showLegacy)
                        logger->stdout("%s: " ANSI_YELLOW "omitted" ANSI_NORMAL " (use '--legacy' to show)", headerPrefix);
                    else {
                        if (visitor.isDerivation())
                            showDerivation();
                        else if (attrPath.size() <= 2)
                            // FIXME: handle recurseIntoAttrs
                            recurse();
                    }
                }

                else if (
                    (attrPath.size() == 2 && attrPath[0] == "defaultApp") ||
                    (attrPath.size() == 3 && attrPath[0] == "apps"))
                {
                    auto aType = visitor.maybeGetAttr("type");
                    if (!aType || aType->getString() != "app")
                        throw EvalError("not an app definition");
                    logger->stdout("%s: app", headerPrefix);
                }

                else {
                    logger->stdout("%s: %s",
                        headerPrefix,
                        attrPath.size() == 1 && attrPath[0] == "overlay" ? "Nixpkgs overlay" :
                        attrPath.size() == 2 && attrPath[0] == "nixosConfigurations" ? "NixOS configuration" :
                        attrPath.size() == 2 && attrPath[0] == "nixosModules" ? "NixOS module" :
                        ANSI_YELLOW "unknown" ANSI_NORMAL);
                }
            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    logger->stdout("%s: " ANSI_RED "%s" ANSI_NORMAL, headerPrefix, e.what());
            }
        };

        auto db = useEvalCache ? std::make_shared<AttrDb>(flake.getFingerprint()) : nullptr;

        auto root = std::make_shared<AttrRoot>(db, *state,
            [&]()
            {
                auto vFlake = state->allocValue();
                flake::callFlake(*state, flake, *vFlake);

                state->forceAttrs(*vFlake);

                auto aOutputs = vFlake->attrs->get(state->symbols.create("outputs"));
                assert(aOutputs);

                return aOutputs->value;
            });

        visit(*root->getRoot(), {}, fmt(ANSI_BOLD "%s" ANSI_NORMAL, flake.flake.lockedRef), "");
    }
};

struct CmdFlake : virtual MultiCommand, virtual Command
{
    CmdFlake()
        : MultiCommand({
                {"list", []() { return make_ref<CmdFlakeList>(); }},
                {"update", []() { return make_ref<CmdFlakeUpdate>(); }},
                {"info", []() { return make_ref<CmdFlakeInfo>(); }},
                {"list-inputs", []() { return make_ref<CmdFlakeListInputs>(); }},
                {"check", []() { return make_ref<CmdFlakeCheck>(); }},
                {"add", []() { return make_ref<CmdFlakeAdd>(); }},
                {"remove", []() { return make_ref<CmdFlakeRemove>(); }},
                {"pin", []() { return make_ref<CmdFlakePin>(); }},
                {"init", []() { return make_ref<CmdFlakeInit>(); }},
                {"clone", []() { return make_ref<CmdFlakeClone>(); }},
                {"archive", []() { return make_ref<CmdFlakeArchive>(); }},
                {"show", []() { return make_ref<CmdFlakeShow>(); }},
            })
    {
    }

    std::string description() override
    {
        return "manage Nix flakes";
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix flake' requires a sub-command.");
        command->prepare();
        command->run();
    }

    void printHelp(const string & programName, std::ostream & out) override
    {
        MultiCommand::printHelp(programName, out);
    }
};

static auto r1 = registerCommand<CmdFlake>("flake");

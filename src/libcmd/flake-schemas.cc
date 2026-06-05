#include "nix/cmd/flake-schemas.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/mounted-source-accessor.hh"

namespace nix::flake_schemas {

using namespace eval_cache;
using namespace flake;

static LockedFlake getBuiltinDefaultSchemasFlake(EvalState & state)
{
    auto accessor = make_ref<MemorySourceAccessor>();

    accessor->setPathDisplay("«builtin-flake-schemas»");

    accessor->addFile(
        CanonPath("flake.nix"),
#include "builtin-flake-schemas.nix.gen.hh"
    );

    auto [storePath, narHash] = state.store->computeStorePath("source", {accessor});

    state.allowPath(storePath); // FIXME: should just whitelist the entire virtual store

    state.storeFS->mount(CanonPath(state.store->printStorePath(storePath)), accessor);

    // Construct a dummy flakeref.
    auto flakeRef = parseFlakeRef(
        fetchSettings,
        fmt("tarball+https://builtin-flake-schemas?narHash=%s", narHash.to_string(HashFormat::SRI, true)));

    auto flake = readFlake(state, flakeRef, flakeRef, flakeRef, state.storePath(storePath), {});

    return lockFlake(flakeSettings, state, flakeRef, {}, flake);
}

ref<EvalCache> call(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake,
    std::optional<FlakeRef> defaultSchemasFlake,
    bool allowEvalCache)
{
    auto fingerprint = lockedFlake->getFingerprint(*state.store, state.fetchSettings);

    std::string callFlakeSchemasNix =
#include "call-flake-schemas.nix.gen.hh"
        ;

    auto lockedDefaultSchemasFlake = defaultSchemasFlake
                                         ? flake::lockFlake(flakeSettings, state, *defaultSchemasFlake, {})
                                         : getBuiltinDefaultSchemasFlake(state);
    auto lockedDefaultSchemasFlakeFingerprint =
        lockedDefaultSchemasFlake.getFingerprint(*state.store, state.fetchSettings);

    std::optional<Fingerprint> fingerprint2;
    if (allowEvalCache && evalSettings.useEvalCache && evalSettings.pureEval && fingerprint
        && lockedDefaultSchemasFlakeFingerprint)
        fingerprint2 = hashString(
            HashAlgorithm::SHA256,
            fmt("app:%s:%s:%s",
                hashString(HashAlgorithm::SHA256, callFlakeSchemasNix).to_string(HashFormat::Base16, false),
                fingerprint->to_string(HashFormat::Base16, false),
                lockedDefaultSchemasFlakeFingerprint->to_string(HashFormat::Base16, false)));

    if (fingerprint2) {
        auto i = state.evalCaches.find(*fingerprint2);
        if (i != state.evalCaches.end())
            return i->second;
    }

    auto cache = make_ref<EvalCache>(
        fingerprint2, state, [&state, lockedFlake, callFlakeSchemasNix, lockedDefaultSchemasFlake]() {
            auto vCallFlakeSchemas = state.allocValue();
            state.eval(
                state.parseExprFromString(callFlakeSchemasNix, state.rootPath(CanonPath::root)), *vCallFlakeSchemas);

            auto vFlake = state.allocValue();
            flake::callFlake(state, *lockedFlake, *vFlake);

            auto vDefaultSchemasFlake = state.allocValue();
            if (vFlake->type() == nAttrs && vFlake->attrs()->get(state.symbols.create("schemas")))
                vDefaultSchemasFlake->mkNull();
            else
                flake::callFlake(state, lockedDefaultSchemasFlake, *vDefaultSchemasFlake);

            auto vRes = state.allocValue();
            Value * args[] = {vDefaultSchemasFlake, vFlake};
            state.callFunction(*vCallFlakeSchemas, args, *vRes, noPos);

            return vRes;
        });

    /* Derive the flake output attribute path from the cursor used to
       traverse the inventory. We do this so we don't have to maintain
       a separate attrpath for that. */
    cache->cleanupAttrPath = [&](AttrPath && attrPath) {
        AttrPath res;
        auto i = attrPath.begin();
        if (i == attrPath.end())
            return attrPath;

        if (state.symbols[*i] == "inventory") {
            ++i;
            if (i != attrPath.end()) {
                res.push_back(*i++); // copy output name
                if (i != attrPath.end())
                    ++i; // skip "outputs"
                while (i != attrPath.end()) {
                    ++i; // skip "children"
                    if (i != attrPath.end())
                        res.push_back(*i++);
                }
            }
        }

        else if (state.symbols[*i] == "outputs") {
            res.insert(res.begin(), ++i, attrPath.end());
        }

        else
            abort();

        return res;
    };

    if (fingerprint2)
        state.evalCaches.emplace(*fingerprint2, cache);

    return cache;
}

void forEachOutput(
    ref<AttrCursor> inventory,
    std::function<void(Symbol outputName, std::shared_ptr<AttrCursor> output, const std::string & doc, bool isLast)> f)
{
    auto outputNames = inventory->getAttrs();

    auto doOutputs = [&](bool allowIFD) {
        evalSettings.enableImportFromDerivation.setDefault(allowIFD);
        for (const auto & [i, outputName] : enumerate(outputNames)) {
            auto outputInfo = inventory->getAttr(outputName);
            try {
                auto allowIFDAttr = outputInfo->maybeGetAttr("allowIFD");
                if (allowIFD != (!allowIFDAttr || allowIFDAttr->getBool()))
                    continue;
                auto isUnknown = (bool) outputInfo->maybeGetAttr("unknown");
                auto output = outputInfo->maybeGetAttr("output");
                if (!output && !isUnknown)
                    // We have a schema but no corresponding output, so skip this.
                    continue;
                Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", outputInfo->getAttrPathStr()));
                f(outputName,
                  isUnknown ? std::shared_ptr<AttrCursor>() : output,
                  isUnknown ? "" : outputInfo->getAttr("doc")->getString(),
                  i + 1 == outputNames.size());
            } catch (Error & e) {
                e.addTrace(nullptr, "while evaluating the flake output '%s':", outputInfo->getAttrPathStr());
                throw;
            }
        }
    };

    // Do outputs that disallow import-from-derivation first. That way, they can't depend on outputs that do allow it.
    doOutputs(false);
    doOutputs(true);
}

void visit(
    std::optional<std::string> system,
    bool includeLegacy,
    ref<AttrCursor> node,
    std::function<void(const Leaf & leaf)> visitLeaf,
    std::function<void(std::function<void(ForEachChild)>)> visitNonLeaf,
    std::function<void(ref<AttrCursor> node, const std::vector<std::string> & systems)> visitFiltered,
    std::function<void(ref<AttrCursor> node)> visitLegacy)
{
    Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", node->getAttrPathStr()));

    /* Filter out legacy outputs, unless --legacy is enabled. */
    if (!includeLegacy) {
        if (auto b = node->maybeGetAttr("isLegacy"); b && b->getBool()) {
            visitLegacy(node);
            return;
        }
    }

    /* Apply the system type filter. */
    if (system) {
        if (auto forSystems = Node(node).forSystems()) {
            if (std::find(forSystems->begin(), forSystems->end(), *system) == forSystems->end()) {
                visitFiltered(node, *forSystems);
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
                    // FIXME: use the `isLegacy` attribute.
                    if (node->root->state.symbols[node->getAttrPath()[0]] != "legacyPackages") {
                        e.addTrace(
                            nullptr, "while evaluating the flake output attribute '%s':", node->getAttrPathStr());
                        throw;
                    }
                }
            }
        });
    }

    else
        visitLeaf(Leaf(node));
}

std::optional<std::vector<std::string>> Node::forSystems() const
{
    if (auto forSystems = node->maybeGetAttr("forSystems"))
        return forSystems->getListOfStrings();
    else
        return std::nullopt;
}

ref<AttrCursor> Node::getOutput(const ref<AttrCursor> & outputs) const
{
    auto res = outputs->findAlongAttrPath(node->getAttrPath());
    if (!res)
        throw Error("flake output '%s' should exist according to its schema, but it doesn't", node->getAttrPathStr());
    return *res;
}

std::optional<std::string> Leaf::what() const
{
    if (auto what = node->maybeGetAttr("what"))
        return what->getString();
    else
        return std::nullopt;
}

std::optional<std::string> Leaf::shortDescription() const
{
    if (auto what = node->maybeGetAttr("shortDescription"))
        return what->getString();
    return std::nullopt;
}

std::optional<AttrPath> Leaf::derivationAttrPath() const
{
    auto n = node->maybeGetAttr("derivationAttrPath");
    if (!n)
        return std::nullopt;
    return AttrPath::fromStrings(node->root->state, n->getListOfStrings());
}

std::shared_ptr<AttrCursor> Leaf::derivation(const ref<AttrCursor> & outputs) const
{
    auto path = derivationAttrPath();
    if (!path) {
        auto n = node->maybeGetAttr("derivation");
        if (n)
            warn(
                "Flake output '%s' has a schema that uses the deprecated 'derivation' attribute instead of 'derivationAttrPath'. "
                "Please update the schema to use 'derivationAttrPath' instead. "
                "You may want to upgrade to version 0.3.0 or higher of https://github.com/DeterminateSystems/flake-schemas.",
                node->getAttrPathStr());
        return n;
    }
    auto drv = getOutput(outputs)->findAlongAttrPath(*path);
    if (!drv)
        throw Error(
            "flake output '%s' does not have a derivation attribute '%s'",
            node->getAttrPathStr(),
            path->to_string(node->root->state));
    return *drv;
}

bool Leaf::isFlakeCheck() const
{
    auto isFlakeCheck = node->maybeGetAttr("isFlakeCheck");
    return isFlakeCheck && isFlakeCheck->getBool();
}

std::optional<OutputInfo> getOutputInfo(ref<AttrCursor> inventory, AttrPath attrPath)
{
    if (attrPath.empty())
        return std::nullopt;

    auto outputName = attrPath.front();

    auto schemaInfo = inventory->maybeGetAttr(outputName);
    if (!schemaInfo)
        return std::nullopt;

    auto node = schemaInfo->maybeGetAttr("output");
    if (!node)
        return std::nullopt;

    auto pathLeft = std::span(attrPath).subspan(1);

    while (!pathLeft.empty()) {
        auto children = node->maybeGetAttr("children");
        if (!children)
            break;
        auto attr = pathLeft.front();
        node = children->maybeGetAttr(attr);
        if (!node)
            return std::nullopt;
        pathLeft = pathLeft.subspan(1);
    }

    return OutputInfo{
        .schemaInfo = ref(schemaInfo),
        .nodeInfo = ref(node),
        .leafAttrPath = AttrPath(pathLeft.begin(), pathLeft.end()),
    };
}

Schemas getSchemas(ref<AttrCursor> inventory)
{
    auto & state(inventory->root->state);

    Schemas schemas;

    for (auto & schemaName : inventory->getAttrs()) {
        auto schema = inventory->getAttr(schemaName);

        SchemaInfo schemaInfo;

        if (auto roles = schema->maybeGetAttr("roles")) {
            for (auto & roleName : roles->getAttrs()) {
                schemaInfo.roles.insert(std::string(state.symbols[roleName]));
            }
        }

        if (auto appendSystem = schema->maybeGetAttr("appendSystem"))
            schemaInfo.appendSystem = appendSystem->getBool();

        if (auto defaultAttrPath = schema->maybeGetAttr("defaultAttrPath")) {
            AttrPath attrPath;
            for (auto & s : defaultAttrPath->getListOfStrings())
                attrPath.push_back(state.symbols.create(s));
            schemaInfo.defaultAttrPath = std::move(attrPath);
        }

        schemas.insert_or_assign(std::string(state.symbols[schemaName]), std::move(schemaInfo));
    }

    return schemas;
}

} // namespace nix::flake_schemas

namespace nix {

MixFlakeSchemas::MixFlakeSchemas()
{
    addFlag(
        {.longName = "default-flake-schemas",
         .description = "The URL of the flake providing default flake schema definitions.",
         .labels = {"flake-ref"},
         .handler = {&defaultFlakeSchemas},
         .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
             completeFlakeRef(completions, getStore(), prefix);
         }}});
}

std::optional<FlakeRef> MixFlakeSchemas::getDefaultFlakeSchemas()
{
    if (!defaultFlakeSchemas)
        return std::nullopt;
    else
        return parseFlakeRef(fetchSettings, *defaultFlakeSchemas, absPath(getCommandBaseDir()));
}

} // namespace nix

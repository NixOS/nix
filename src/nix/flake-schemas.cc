#include "flake-schemas.hh"
#include "eval-settings.hh"
#include "fetch-to-store.hh"
#include "memory-source-accessor.hh"

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

    // FIXME: remove this when we have lazy trees.
    auto storePath = fetchToStore(*state.store, {accessor}, FetchMode::Copy);
    state.allowPath(storePath);

    // Construct a dummy flakeref.
    auto flakeRef = parseFlakeRef(
        fmt("tarball+https://builtin-flake-schemas?narHash=%s",
            state.store->queryPathInfo(storePath)->narHash.to_string(HashFormat::SRI, true)));

    auto flake = readFlake(state, flakeRef, flakeRef, flakeRef, state.rootPath(state.store->toRealPath(storePath)), {});

    return lockFlake(state, flakeRef, {}, flake);
}

std::tuple<ref<EvalCache>, ref<eval_cache::AttrCursor>>
call(EvalState & state, std::shared_ptr<flake::LockedFlake> lockedFlake, std::optional<FlakeRef> defaultSchemasFlake)
{
    auto fingerprint = lockedFlake->getFingerprint(state.store);

    std::string callFlakeSchemasNix =
#include "call-flake-schemas.nix.gen.hh"
        ;

    auto lockedDefaultSchemasFlake =
        defaultSchemasFlake ? flake::lockFlake(state, *defaultSchemasFlake, {}) : getBuiltinDefaultSchemasFlake(state);
    auto lockedDefaultSchemasFlakeFingerprint = lockedDefaultSchemasFlake.getFingerprint(state.store);

    std::optional<Fingerprint> fingerprint2;
    if (fingerprint && lockedDefaultSchemasFlakeFingerprint)
        fingerprint2 = hashString(
            HashAlgorithm::SHA256,
            fmt("app:%s:%s:%s",
                hashString(HashAlgorithm::SHA256, callFlakeSchemasNix).to_string(HashFormat::Base16, false),
                fingerprint->to_string(HashFormat::Base16, false),
                lockedDefaultSchemasFlakeFingerprint->to_string(HashFormat::Base16, false)));

    // FIXME: merge with openEvalCache().
    auto cache = make_ref<EvalCache>(
        evalSettings.useEvalCache && evalSettings.pureEval ? fingerprint2 : std::nullopt,
        state,
        [&state, lockedFlake, callFlakeSchemasNix, lockedDefaultSchemasFlake]() {
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
            state.callFunction(*vCallFlakeSchemas, 2, args, *vRes, noPos);

            return vRes;
        });

    return {cache, cache->getRoot()->getAttr("inventory")};
}

/* Derive the flake output attribute path from the cursor used to
   traverse the inventory. We do this so we don't have to maintain a
   separate attrpath for that. */
std::vector<Symbol> toAttrPath(ref<AttrCursor> cursor)
{
    auto attrPath = cursor->getAttrPath();
    std::vector<Symbol> res;
    auto i = attrPath.begin();
    assert(i != attrPath.end());
    ++i; // skip "inventory"
    assert(i != attrPath.end());
    res.push_back(*i++); // copy output name
    if (i != attrPath.end())
        ++i; // skip "outputs"
    while (i != attrPath.end()) {
        ++i; // skip "children"
        if (i != attrPath.end())
            res.push_back(*i++);
    }
    return res;
}

std::string toAttrPathStr(ref<AttrCursor> cursor)
{
    return concatStringsSep(".", cursor->root->state.symbols.resolve(toAttrPath(cursor)));
}

void forEachOutput(
    ref<AttrCursor> inventory,
    std::function<void(Symbol outputName, std::shared_ptr<AttrCursor> output, const std::string & doc, bool isLast)> f)
{
    // FIXME: handle non-IFD outputs first.
    // evalSettings.enableImportFromDerivation.setDefault(false);

    auto outputNames = inventory->getAttrs();
    for (const auto & [i, outputName] : enumerate(outputNames)) {
        auto output = inventory->getAttr(outputName);
        try {
            auto isUnknown = (bool) output->maybeGetAttr("unknown");
            Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", toAttrPathStr(output)));
            f(outputName,
              isUnknown ? std::shared_ptr<AttrCursor>() : output->getAttr("output"),
              isUnknown ? "" : output->getAttr("doc")->getString(),
              i + 1 == outputNames.size());
        } catch (Error & e) {
            e.addTrace(nullptr, "while evaluating the flake output '%s':", toAttrPathStr(output));
            throw;
        }
    }
}

void visit(
    std::optional<std::string> system,
    ref<AttrCursor> node,
    std::function<void(ref<AttrCursor> leaf)> visitLeaf,
    std::function<void(std::function<void(ForEachChild)>)> visitNonLeaf,
    std::function<void(ref<AttrCursor> node, const std::vector<std::string> & systems)> visitFiltered)
{
    Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", toAttrPathStr(node)));

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
                    if (node->root->state.symbols[toAttrPath(node)[0]] != "legacyPackages") {
                        e.addTrace(nullptr, "while evaluating the flake output attribute '%s':", toAttrPathStr(node));
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
        if (s != "")
            return s;
    }
    return std::nullopt;
}

std::shared_ptr<AttrCursor> derivation(ref<AttrCursor> leaf)
{
    return leaf->maybeGetAttr("derivation");
}

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
        return parseFlakeRef(*defaultFlakeSchemas, absPath("."));
}

}

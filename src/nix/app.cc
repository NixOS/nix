#include "installables.hh"
#include "store-api.hh"
#include "eval-inline.hh"
#include "eval-cache.hh"
#include "names.hh"
#include "command.hh"
#include "derivations.hh"

namespace nix {

struct InstallableDerivedPath : Installable
{
    ref<Store> store;
    const DerivedPath derivedPath;

    InstallableDerivedPath(ref<Store> store, const DerivedPath & derivedPath)
        : store(store)
        , derivedPath(derivedPath)
    {
    }


    std::string what() const override { return derivedPath.to_string(*store); }

    DerivedPaths toDerivedPaths() override
    {
        return {derivedPath};
    }

    std::optional<StorePath> getStorePath() override
    {
        return std::nullopt;
    }
};

/**
 * Return the rewrites that are needed to resolve a string whose context is
 * included in `dependencies`.
 */
StringPairs resolveRewrites(Store & store, const BuiltPaths dependencies)
{
    StringPairs res;
    for (auto & dep : dependencies)
        if (auto drvDep = std::get_if<BuiltPathBuilt>(&dep))
            for (auto & [ outputName, outputPath ] : drvDep->outputs)
                res.emplace(
                    downstreamPlaceholder(store, drvDep->drvPath, outputName),
                    store.printStorePath(outputPath)
                );
    return res;
}

/**
 * Resolve the given string assuming the given context.
 */
std::string resolveString(Store & store, const std::string & toResolve, const BuiltPaths dependencies)
{
    auto rewrites = resolveRewrites(store, dependencies);
    return rewriteStrings(toResolve, rewrites);
}

UnresolvedApp Installable::toApp(EvalState & state)
{
    auto cursor = getCursor(state);
    auto attrPath = cursor->getAttrPath();

    auto type = cursor->getAttr("type")->getString();

    std::string expected = !attrPath.empty() && attrPath[0] == "apps" ? "app" : "derivation";
    if (type != expected)
        throw Error("attribute '%s' should have type '%s'", cursor->getAttrPathStr(), expected);

    if (type == "app") {
        auto [program, context] = cursor->getAttr("program")->getStringWithContext();

        std::vector<StorePathWithOutputs> context2;
        for (auto & [path, name] : context)
            context2.push_back({path, {name}});

        return UnresolvedApp{App {
            .context = std::move(context2),
            .program = program,
        }};
    }

    else if (type == "derivation") {
        auto drvPath = cursor->forceDerivation();
        auto outPath = cursor->getAttr(state.sOutPath)->getString();
        auto outputName = cursor->getAttr(state.sOutputName)->getString();
        auto name = cursor->getAttr(state.sName)->getString();
        auto aPname = cursor->maybeGetAttr("pname");
        auto aMeta = cursor->maybeGetAttr("meta");
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr("mainProgram") : nullptr;
        auto mainProgram =
            aMainProgram
            ? aMainProgram->getString()
            : aPname
            ? aPname->getString()
            : DrvName(name).name;
        auto program = outPath + "/bin/" + mainProgram;
        return UnresolvedApp { App {
            .context = { { drvPath, {outputName} } },
            .program = program,
        }};
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", cursor->getAttrPathStr(), type);
}

// FIXME: move to libcmd
App UnresolvedApp::resolve(ref<Store> evalStore, ref<Store> store)
{
    auto res = unresolved;

    std::vector<std::shared_ptr<Installable>> installableContext;

    for (auto & ctxElt : unresolved.context)
        installableContext.push_back(
            std::make_shared<InstallableDerivedPath>(store, ctxElt.toDerivedPath()));

    auto builtContext = Installable::build(evalStore, store, Realise::Outputs, installableContext);
    res.program = resolveString(*store, unresolved.program, builtContext);
    if (!store->isInStore(res.program))
        throw Error("app program '%s' is not in the Nix store", res.program);

    return res;
}

}

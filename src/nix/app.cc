#include "installables.hh"
#include "store-api.hh"
#include "eval-inline.hh"
#include "eval-cache.hh"
#include "names.hh"
#include "command.hh"

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


    std::string what() override { return derivedPath.to_string(*store); }

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
 * included in `dependencies`
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
 * Resolve the given string assuming the given context
 */
std::string resolveString(Store & store, const std::string & toResolve, const BuiltPaths dependencies)
{
    auto rewrites = resolveRewrites(store, dependencies);
    return rewriteStrings(toResolve, rewrites);
}

App Installable::toApp(EvalState & state)
{
    auto [cursor, attrPath] = getCursor(state);

    auto type = cursor->getAttr("type")->getString();

    auto checkProgram = [&](const Path & program)
    {
        if (!state.store->isInStore(program))
            throw Error("app program '%s' is not in the Nix store", program);
    };

    std::vector<std::shared_ptr<Installable>> context;
    std::string unresolvedProgram;


    if (type == "app") {
        auto [program, context_] = cursor->getAttr("program")->getStringWithContext();
        unresolvedProgram = program;

        for (auto & [path, name] : context_)
            context.push_back(std::make_shared<InstallableDerivedPath>(
                state.store,
                DerivedPathBuilt{
                    .drvPath = state.store->parseStorePath(path),
                    .outputs = {name},
                }));
    }

    else if (type == "derivation") {
        auto drvPath = cursor->forceDerivation();
        auto outPath = cursor->getAttr(state.sOutPath)->getString();
        auto outputName = cursor->getAttr(state.sOutputName)->getString();
        auto name = cursor->getAttr(state.sName)->getString();
        auto aMeta = cursor->maybeGetAttr("meta");
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr("mainProgram") : nullptr;
        auto mainProgram =
            aMainProgram
            ? aMainProgram->getString()
            : DrvName(name).name;
        unresolvedProgram = outPath + "/bin/" + mainProgram;
        context = {std::make_shared<InstallableDerivedPath>(
            state.store,
            DerivedPathBuilt{
                .drvPath = drvPath,
                .outputs = {outputName},
            })};
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", attrPath, type);

    auto builtContext = build(state.store, Realise::Outputs, context);
    auto program = resolveString(*state.store, unresolvedProgram, builtContext);
    checkProgram(program);
    return App {
        .program = program,
    };
}

}

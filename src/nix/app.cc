#include "installables.hh"
#include "store-api.hh"
#include "eval-inline.hh"
#include "eval-cache.hh"
#include "names.hh"

namespace nix {

App Installable::toApp(EvalState & state)
{
    auto [cursor, attrPath] = getCursor(state);

    auto type = cursor->getAttr("type")->getString();

    if (type == "app") {
        auto [program, context] = cursor->getAttr("program")->getStringWithContext();

        if (!state.store->isInStore(program))
            throw Error("app program '%s' is not in the Nix store", program);

        std::vector<StorePathWithOutputs> context2;
        for (auto & [path, name] : context)
            context2.push_back({state.store->parseStorePath(path), {name}});

        return App {
            .context = std::move(context2),
            .program = program,
        };
    }

    auto getDerivation = [&](std::shared_ptr<eval_cache::AttrCursor> attr)
    {
        auto drvPath = attr->forceDerivation();
        auto outPath = attr->getAttr(state.sOutPath)->getString();
        auto outputName = attr->getAttr(state.sOutputName)->getString();
        auto name = attr->getAttr(state.sName)->getString();
        return App {
            .context = { { drvPath, {outputName} } },
            .program = outPath + "/bin/" + DrvName(name).name,
        };
    };

    if (type == "derivation")
        return getDerivation(cursor);

    if (type == "module") {
        // FIXME: define an 'app' option.
        auto aDerivation = cursor->findAlongAttrPath(
            {state.symbols.create("final"), state.symbols.create("derivation")});
        if (aDerivation)
            return getDerivation(aDerivation);
    }

    throw Error("attribute '%s' has unsupported type '%s'", attrPath, type);
}

}

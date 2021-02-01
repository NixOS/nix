#include "installables.hh"
#include "store-api.hh"
#include "eval-inline.hh"
#include "names.hh"

namespace nix {

App Installable::toApp(EvalState & state)
{
    auto [value, pos, attrPath] = toValue(state);

    auto type = state.forceStringNoCtx(*state.getAttrField(*value, {state.sType}, pos));

    if (type == "app") {
        StringSet context;
        auto program = state.forceString(*state.getAttrField(*value, {state.symbols.create("program")}, pos));

        if (!state.store->isInStore(program))
            throw Error("app program '%s' is not in the Nix store", program);

        std::vector<StorePathWithOutputs> context2;
        for (auto & rawCtxItem : context) {
            auto [path, name] = decodeContext(rawCtxItem);
            context2.push_back({state.store->parseStorePath(path), {name}});
        }

        return App {
            .context = std::move(context2),
            .program = program,
        };
    }

    else if (type == "derivation") {
        auto drvPath = state.store->parseStorePath(state.forceString(*state.getAttrField(*value, {state.sDrvPath}, pos)));
        auto outPath = state.forceString(*state.getAttrField(*value, {state.sOutPath}, pos));
        auto outputName = state.forceStringNoCtx(*state.getAttrField(*value, {state.sOutputName}, pos));
        auto name = state.forceStringNoCtx(*state.getAttrField(*value, {state.sName}, pos));
        return App {
            .context = { { drvPath, {outputName} } },
            .program = outPath + "/bin/" + DrvName(name).name,
        };
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", attrPath, type);
}

}

#include "built-path.hh"
#include "derivations.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

#include <optional>

namespace nix {

#define CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, COMPARATOR) \
    bool MY_TYPE ::operator COMPARATOR (const MY_TYPE & other) const \
    { \
        const MY_TYPE* me = this; \
        auto fields1 = std::tie(*me->drvPath, me->FIELD); \
        me = &other; \
        auto fields2 = std::tie(*me->drvPath, me->FIELD); \
        return fields1 COMPARATOR fields2; \
    }
#define CMP(CHILD_TYPE, MY_TYPE, FIELD) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, ==) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, !=) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, <)

#define FIELD_TYPE std::pair<std::string, StorePath>
CMP(SingleBuiltPath, SingleBuiltPathBuilt, output)
#undef FIELD_TYPE

#define FIELD_TYPE std::map<std::string, StorePath>
CMP(SingleBuiltPath, BuiltPathBuilt, outputs)
#undef FIELD_TYPE

#undef CMP
#undef CMP_ONE

StorePath SingleBuiltPath::outPath() const
{
    return std::visit(
        overloaded{
            [](const SingleBuiltPath::Opaque & p) { return p.path; },
            [](const SingleBuiltPath::Built & b) { return b.output.second; },
        }, raw()
    );
}

StorePathSet BuiltPath::outPaths() const
{
    return std::visit(
        overloaded{
            [](const BuiltPath::Opaque & p) { return StorePathSet{p.path}; },
            [](const BuiltPath::Built & b) {
                StorePathSet res;
                for (auto & [_, path] : b.outputs)
                    res.insert(path);
                return res;
            },
        }, raw()
    );
}

SingleDerivedPath::Built SingleBuiltPath::Built::discardOutputPath() const
{
    return SingleDerivedPath::Built {
        .drvPath = make_ref<SingleDerivedPath>(drvPath->discardOutputPath()),
        .output = output.first,
    };
}

SingleDerivedPath SingleBuiltPath::discardOutputPath() const
{
    return std::visit(
        overloaded{
            [](const SingleBuiltPath::Opaque & p) -> SingleDerivedPath {
                return p;
            },
            [](const SingleBuiltPath::Built & b) -> SingleDerivedPath {
                return b.discardOutputPath();
            },
        }, raw()
    );
}

nlohmann::json BuiltPath::Built::toJSON(const StoreDirConfig & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    for (const auto & [outputName, outputPath] : outputs) {
        res["outputs"][outputName] = store.printStorePath(outputPath);
    }
    return res;
}

nlohmann::json SingleBuiltPath::Built::toJSON(const StoreDirConfig & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    auto & [outputName, outputPath] = output;
    res["output"] = outputName;
    res["outputPath"] = store.printStorePath(outputPath);
    return res;
}

nlohmann::json SingleBuiltPath::toJSON(const StoreDirConfig & store) const
{
    return std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw());
}

nlohmann::json BuiltPath::toJSON(const StoreDirConfig & store) const
{
    return std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw());
}

RealisedPath::Set BuiltPath::toRealisedPaths(Store & store) const
{
    RealisedPath::Set res;
    std::visit(
        overloaded{
            [&](const BuiltPath::Opaque & p) { res.insert(p.path); },
            [&](const BuiltPath::Built & p) {
                auto drvHashes =
                    staticOutputHashes(store, store.readDerivation(p.drvPath->outPath()));
                for (auto& [outputName, outputPath] : p.outputs) {
                    if (experimentalFeatureSettings.isEnabled(
                                Xp::CaDerivations)) {
                        auto drvOutput = get(drvHashes, outputName);
                        if (!drvOutput)
                            throw Error(
                                "the derivation '%s' has unrealised output '%s' (derived-path.cc/toRealisedPaths)",
                                store.printStorePath(p.drvPath->outPath()), outputName);
                        auto thisRealisation = store.queryRealisation(
                            DrvOutput{*drvOutput, outputName});
                        assert(thisRealisation);  // We’ve built it, so we must
                                                  // have the realisation
                        res.insert(*thisRealisation);
                    } else {
                        res.insert(outputPath);
                    }
                }
            },
        },
        raw());
    return res;
}

}

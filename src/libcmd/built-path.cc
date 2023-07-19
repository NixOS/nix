#include "built-path.hh"
#include "derivations.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

#include <optional>

namespace nix {

nlohmann::json BuiltPath::Built::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["drvPath"] = store->printStorePath(drvPath);
    for (const auto& [output, path] : outputs) {
        res["outputs"][output] = store->printStorePath(path);
    }
    return res;
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

RealisedPath::Set BuiltPath::toRealisedPaths(Store & store) const
{
    RealisedPath::Set res;
    std::visit(
        overloaded{
            [&](const BuiltPath::Opaque & p) { res.insert(p.path); },
            [&](const BuiltPath::Built & p) {
                auto drvHashes =
                    staticOutputHashes(store, store.readDerivation(p.drvPath));
                for (auto& [outputName, outputPath] : p.outputs) {
                    if (experimentalFeatureSettings.isEnabled(
                                Xp::CaDerivations)) {
                        auto drvOutput = get(drvHashes, outputName);
                        if (!drvOutput)
                            throw Error(
                                "the derivation '%s' has unrealised output '%s' (derived-path.cc/toRealisedPaths)",
                                store.printStorePath(p.drvPath), outputName);
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

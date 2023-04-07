#include "derived-path.hh"
#include "derivations.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

#include <optional>

namespace nix {

nlohmann::json DerivedPath::Opaque::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["path"] = store->printStorePath(path);
    return res;
}

nlohmann::json DerivedPath::Built::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["drvPath"] = store->printStorePath(drvPath);
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    const auto outputMap = store->queryPartialDerivationOutputMap(drvPath);
    for (const auto & [output, outputPathOpt] : outputMap) {
        if (!outputs.contains(output)) continue;
        if (outputPathOpt)
            res["outputs"][output] = store->printStorePath(*outputPathOpt);
        else
            res["outputs"][output] = nullptr;
    }
    return res;
}

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

std::string DerivedPath::Opaque::to_string(const Store & store) const
{
    return store.printStorePath(path);
}

std::string DerivedPath::Built::to_string(const Store & store) const
{
    return store.printStorePath(drvPath)
        + "!"
        + outputs.to_string();
}

std::string DerivedPath::to_string(const Store & store) const
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        this->raw());
}


DerivedPath::Opaque DerivedPath::Opaque::parse(const Store & store, std::string_view s)
{
    return {store.parseStorePath(s)};
}

DerivedPath::Built DerivedPath::Built::parse(const Store & store, std::string_view drvS, std::string_view outputsS)
{
    return {
        .drvPath = store.parseStorePath(drvS),
        .outputs = OutputsSpec::parse(outputsS),
    };
}

DerivedPath DerivedPath::parse(const Store & store, std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? (DerivedPath) DerivedPath::Opaque::parse(store, s)
        : (DerivedPath) DerivedPath::Built::parse(store, s.substr(0, n), s.substr(n + 1));
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

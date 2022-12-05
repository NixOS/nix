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
    const auto knownOutputs = store->queryPartialDerivationOutputMap(drvPath);
    for (const auto& output : outputs) {
        auto knownOutput = get(knownOutputs, output);
        res["outputs"][output] = (knownOutput && *knownOutput)
          ? store->printStorePath(**knownOutput)
          : nullptr;
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
        + (outputs.empty() ? std::string { "*" } : concatStringsSep(",", outputs));
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

DerivedPath::Built DerivedPath::Built::parse(const Store & store, std::string_view s)
{
    size_t n = s.find("!");
    assert(n != s.npos);
    auto drvPath = store.parseStorePath(s.substr(0, n));
    auto outputsS = s.substr(n + 1);
    std::set<std::string> outputs;
    if (outputsS != "*")
        outputs = tokenizeString<std::set<std::string>>(outputsS, ",");
    return {drvPath, outputs};
}

DerivedPath DerivedPath::parse(const Store & store, std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? (DerivedPath) DerivedPath::Opaque::parse(store, s)
        : (DerivedPath) DerivedPath::Built::parse(store, s);
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
                    if (settings.isExperimentalFeatureEnabled(
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

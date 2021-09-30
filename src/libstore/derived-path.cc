#include "derived-path.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix {

nlohmann::json DerivedPath::Opaque::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["path"] = store->printStorePath(path);
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
            [](BuiltPath::Opaque p) { return StorePathSet{p.path}; },
            [](BuiltPath::Built b) {
                StorePathSet res;
                for (auto & [_, path] : b.outputs)
                    res.insert(path);
                return res;
            },
        }, raw()
    );
}

nlohmann::json derivedPathsWithHintsToJSON(const BuiltPaths & buildables, ref<Store> store) {
    auto res = nlohmann::json::array();
    for (const BuiltPath & buildable : buildables) {
        std::visit([&res, store](const auto & buildable) {
            res.push_back(buildable.toJSON(store));
        }, buildable.raw());
    }
    return res;
}


std::string DerivedPath::Opaque::to_string(const Store & store) const {
    return store.printStorePath(path);
}

std::string DerivedPath::Built::to_string(const Store & store) const {
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
    std::set<string> outputs;
    if (outputsS != "*")
        outputs = tokenizeString<std::set<string>>(outputsS, ",");
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
            [&](BuiltPath::Opaque p) { res.insert(p.path); },
            [&](BuiltPath::Built p) {
                auto drvHashes =
                    staticOutputHashes(store, store.readDerivation(p.drvPath));
                for (auto& [outputName, outputPath] : p.outputs) {
                    if (settings.isExperimentalFeatureEnabled(
                            "ca-derivations")) {
                        auto thisRealisation = store.queryRealisation(
                            DrvOutput{drvHashes.at(outputName), outputName});
                        assert(thisRealisation);  // We’ve built it, so we must h
                                                  // ve the realisation
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

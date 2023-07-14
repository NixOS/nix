#include "derived-path.hh"
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

std::string DerivedPath::Opaque::to_string(const Store & store) const
{
    return store.printStorePath(path);
}

std::string DerivedPath::Built::to_string(const Store & store) const
{
    return store.printStorePath(drvPath)
        + '^'
        + outputs.to_string();
}

std::string DerivedPath::Built::to_string_legacy(const Store & store) const
{
    return store.printStorePath(drvPath)
        + '!'
        + outputs.to_string();
}

std::string DerivedPath::to_string(const Store & store) const
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & req) { return req.to_string(store); },
        [&](const DerivedPath::Opaque & req) { return req.to_string(store); },
    }, this->raw());
}

std::string DerivedPath::to_string_legacy(const Store & store) const
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & req) { return req.to_string_legacy(store); },
        [&](const DerivedPath::Opaque & req) { return req.to_string(store); },
    }, this->raw());
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

static inline DerivedPath parseWith(const Store & store, std::string_view s, std::string_view separator)
{
    size_t n = s.find(separator);
    return n == s.npos
        ? (DerivedPath) DerivedPath::Opaque::parse(store, s)
        : (DerivedPath) DerivedPath::Built::parse(store, s.substr(0, n), s.substr(n + 1));
}

DerivedPath DerivedPath::parse(const Store & store, std::string_view s)
{
	return parseWith(store, s, "^");
}

DerivedPath DerivedPath::parseLegacy(const Store & store, std::string_view s)
{
	return parseWith(store, s, "!");
}

}

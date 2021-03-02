#include "buildable.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix {

nlohmann::json BuildableOpaque::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["path"] = store->printStorePath(path);
    return res;
}

template<>
nlohmann::json BuildableFromDrv::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["drvPath"] = store->printStorePath(drvPath);
    for (const auto& [output, path] : outputs) {
        res["outputs"][output] = path ? store->printStorePath(*path) : "";
    }
    return res;
}

nlohmann::json buildablesToJSON(const Buildables & buildables, ref<Store> store) {
    auto res = nlohmann::json::array();
    for (const Buildable & buildable : buildables) {
        std::visit([&res, store](const auto & buildable) {
            res.push_back(buildable.toJSON(store));
        }, buildable);
    }
    return res;
}


std::string BuildableOpaque::to_string(const Store & store) const {
    return store.printStorePath(path);
}

template<>
std::string BuildableReqFromDrv::to_string(const Store & store) const {
    return store.printStorePath(drvPath)
        + "!"
        + (outputs.empty() ? std::string { "*" } : concatStringsSep(",", outputs));
}

std::string to_string(const Store & store, const BuildableReq & req)
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        req);
}


BuildableOpaque BuildableOpaque::parse(const Store & store, std::string_view s)
{
    return {store.parseStorePath(s)};
}

template<>
BuildableReqFromDrv BuildableReqFromDrv::parse(const Store & store, std::string_view s)
{
    size_t n = s.find("!");
    assert(n != s.npos);
    auto drvPath = store.parseStorePath(s.substr(0, n));
    auto outputsS = s.substr(n + 1);
    std::set<string> outputs;
    if (outputsS != "*")
        outputs = tokenizeString<std::set<string>>(outputsS);
    return {drvPath, outputs};
}

BuildableReq parseBuildableReq(const Store & store, std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? (BuildableReq) BuildableOpaque::parse(store, s)
        : (BuildableReq) BuildableReqFromDrv::parse(store, s);
}

}

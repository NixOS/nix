#include "derived-path.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix {

nlohmann::json DerivedPath::Opaque::toJSON(const Store & store) const
{
    nlohmann::json res;
    res["path"] = store.printStorePath(path);
    return res;
}

static void setOutputs(const Store & store, nlohmann::json & res, const std::pair<std::string, std::optional<StorePath>> & output)
{
    auto & [outputName, optOutputPath] = output;
    res["output"] = outputName;
    res["outputPath"] = optOutputPath ? store.printStorePath(*optOutputPath) : "";
}

static void setOutputs(const Store & store, nlohmann::json & res, const std::map<std::string, std::optional<StorePath>> & outputs)
{
    for (const auto & [output, path] : outputs) {
        res["outputs"][output] = path ? store.printStorePath(*path) : "";
    }
}

nlohmann::json DerivedPathWithHints::Built::toJSON(const Store & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    setOutputs(store, res, outputs);
    return res;
}

nlohmann::json SingleDerivedPathWithHints::Built::toJSON(const Store & store) const
{
    nlohmann::json res;
    res["drvPath"] = drvPath->toJSON(store);
    setOutputs(store, res, outputs);
    return res;
}

nlohmann::json SingleDerivedPathWithHints::toJSON(const Store & store) const
{
    return std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw());
}

nlohmann::json DerivedPathWithHints::toJSON(const Store & store) const
{
    return std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw());
}

nlohmann::json derivedPathsWithHintsToJSON(const DerivedPathsWithHints & buildables, const Store & store)
{
    auto res = nlohmann::json::array();
    for (const DerivedPathWithHints & buildable : buildables)
        res.push_back(buildable.toJSON(store));
    return res;
}


std::string DerivedPath::Opaque::to_string(const Store & store) const
{
    return store.printStorePath(path);
}

std::string SingleDerivedPath::Built::to_string(const Store & store) const
{
    return drvPath->to_string(store) + "!" + outputs;
}

std::string DerivedPath::Built::to_string(const Store & store) const
{
    return drvPath->to_string(store)
        + "!"
        + (outputs.empty() ? std::string { "*" } : concatStringsSep(",", outputs));
}

std::string SingleDerivedPath::to_string(const Store & store) const
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        raw());
}

std::string DerivedPath::to_string(const Store & store) const
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        this->raw());
}


DerivedPath SingleDerivedPath::to_multi() const
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & bo) -> DerivedPath {
            return bo;
        },
        [&](const SingleDerivedPath::Built & bfd) -> DerivedPath {
            return DerivedPath::Built { bfd.drvPath, { bfd.outputs } };
        },
    }, this->raw());
}


DerivedPath::Opaque DerivedPath::Opaque::parse(const Store & store, std::string_view s)
{
    return {store.parseStorePath(s)};
}

SingleDerivedPath::Built SingleDerivedPath::Built::parse(const Store & store, std::string_view drvS, std::string_view output)
{
    auto drvPath = std::make_shared<SingleDerivedPath>(
        SingleDerivedPath::parse(store, drvS));
    return { std::move(drvPath), std::string { output } };
}

DerivedPath::Built DerivedPath::Built::parse(const Store & store, std::string_view drvS, std::string_view outputsS)
{
    auto drvPath = std::make_shared<SingleDerivedPath>(
        SingleDerivedPath::parse(store, drvS));
    std::set<string> outputs;
    if (outputsS != "*")
        outputs = tokenizeString<std::set<string>>(outputsS);
    return { std::move(drvPath), std::move(outputs) };
}

SingleDerivedPath SingleDerivedPath::parse(const Store & store, std::string_view s)
{
    size_t n = s.rfind("!");
    return n == s.npos
        ? (SingleDerivedPath) DerivedPath::Opaque::parse(store, s)
        : (SingleDerivedPath) SingleDerivedPath::Built::parse(store, s.substr(0, n), s.substr(n + 1));
}

DerivedPath DerivedPath::parse(const Store & store, std::string_view s)
{
    size_t n = s.rfind("!");
    return n == s.npos
        ? (DerivedPath) DerivedPath::Opaque::parse(store, s)
        : (DerivedPath) DerivedPath::Built::parse(store, s.substr(0, n), s.substr(n + 1));
}

}

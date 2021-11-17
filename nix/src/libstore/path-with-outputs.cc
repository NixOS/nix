#include "path-with-outputs.hh"
#include "store-api.hh"

#include <regex>

namespace nix {

std::string StorePathWithOutputs::to_string(const Store & store) const
{
    return outputs.empty()
        ? store.printStorePath(path)
        : store.printStorePath(path) + "!" + concatStringsSep(",", outputs);
}


DerivedPath StorePathWithOutputs::toDerivedPath() const
{
    if (!outputs.empty()) {
        return DerivedPath::Built { path, OutputsSpec::Names { outputs } };
    } else if (path.isDerivation()) {
        assert(outputs.empty());
        return DerivedPath::Built { path, OutputsSpec::All { } };
    } else {
        return DerivedPath::Opaque { path };
    }
}


std::vector<DerivedPath> toDerivedPaths(const std::vector<StorePathWithOutputs> ss)
{
    std::vector<DerivedPath> reqs;
    for (auto & s : ss) reqs.push_back(s.toDerivedPath());
    return reqs;
}


std::variant<StorePathWithOutputs, StorePath> StorePathWithOutputs::tryFromDerivedPath(const DerivedPath & p)
{
    return std::visit(overloaded {
        [&](const DerivedPath::Opaque & bo) -> std::variant<StorePathWithOutputs, StorePath> {
            if (bo.path.isDerivation()) {
                // drv path gets interpreted as "build", not "get drv file itself"
                return bo.path;
            }
            return StorePathWithOutputs { bo.path };
        },
        [&](const DerivedPath::Built & bfd) -> std::variant<StorePathWithOutputs, StorePath> {
            return StorePathWithOutputs {
                .path = bfd.drvPath,
                // Use legacy encoding of wildcard as empty set
                .outputs = std::visit(overloaded {
                    [&](const OutputsSpec::All &) -> StringSet {
                        return {};
                    },
                    [&](const OutputsSpec::Names & outputs) {
                        return static_cast<StringSet>(outputs);
                    },
                }, bfd.outputs.raw()),
            };
        },
    }, p.raw());
}


std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? std::make_pair(s, std::set<std::string>())
        : std::make_pair(s.substr(0, n),
            tokenizeString<std::set<std::string>>(s.substr(n + 1), ","));
}


StorePathWithOutputs parsePathWithOutputs(const Store & store, std::string_view pathWithOutputs)
{
    auto [path, outputs] = parsePathWithOutputs(pathWithOutputs);
    return StorePathWithOutputs { store.parseStorePath(path), std::move(outputs) };
}


StorePathWithOutputs followLinksToStorePathWithOutputs(const Store & store, std::string_view pathWithOutputs)
{
    auto [path, outputs] = parsePathWithOutputs(pathWithOutputs);
    return StorePathWithOutputs { store.followLinksToStorePath(path), std::move(outputs) };
}

}

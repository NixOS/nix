#include "path-with-outputs.hh"
#include "store-api.hh"

namespace nix {

std::string StorePathWithOutputs::to_string(const Store & store) const
{
    return outputs.empty()
        ? store.printStorePath(path)
        : store.printStorePath(path) + "!" + concatStringsSep(",", outputs);
}


DerivedPath StorePathWithOutputs::toDerivedPath() const
{
    if (!outputs.empty() || path.isDerivation())
        return DerivedPath::Built {
            std::make_shared<SingleDerivedPath>(SingleDerivedPath::Opaque { path }),
            outputs,
        };
    else
        return DerivedPath::Opaque { path };
}


std::vector<DerivedPath> toDerivedPaths(const std::vector<StorePathWithOutputs> ss)
{
    std::vector<DerivedPath> reqs;
    for (auto & s : ss) reqs.push_back(s.toDerivedPath());
    return reqs;
}


StorePathWithOutputs::ParseResult StorePathWithOutputs::tryFromDerivedPath(const DerivedPath & p)
{
    return std::visit(overloaded {
        [&](DerivedPath::Opaque bo) -> StorePathWithOutputs::ParseResult {
            if (bo.path.isDerivation()) {
                // drv path gets interpreted as "build", not "get drv file itself"
                return bo.path;
            }
            return StorePathWithOutputs { bo.path };
        },
        [&](DerivedPath::Built bfd) -> StorePathWithOutputs::ParseResult {
            return std::visit(overloaded {
                [&](SingleDerivedPath::Opaque bo) -> StorePathWithOutputs::ParseResult {
                    return StorePathWithOutputs { bo.path, bfd.outputs };
                },
                [&](SingleDerivedPath::Built) -> StorePathWithOutputs::ParseResult {
                    return std::monostate {};
                },
            }, bfd.drvPath->raw());
        },
    }, p.raw());
}


std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? std::make_pair(s, std::set<string>())
        : std::make_pair(((std::string_view) s).substr(0, n),
            tokenizeString<std::set<string>>(((std::string_view) s).substr(n + 1), ","));
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

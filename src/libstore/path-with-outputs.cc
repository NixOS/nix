#include "path-with-outputs.hh"
#include "store-api.hh"

namespace nix {

std::string StorePathWithOutputs::to_string(const Store & store) const
{
    return outputs.empty()
        ? store.printStorePath(path)
        : store.printStorePath(path) + "!" + concatStringsSep(",", outputs);
}


BuildableReq StorePathWithOutputs::toBuildableReq() const
{
    if (!outputs.empty() || path.isDerivation())
        return BuildableReqFromDrv { path, outputs };
    else
        return BuildableOpaque { path };
}


std::vector<BuildableReq> toBuildableReqs(const std::vector<StorePathWithOutputs> ss)
{
	std::vector<BuildableReq> reqs;
	for (auto & s : ss) reqs.push_back(s.toBuildableReq());
	return reqs;
}


std::variant<StorePathWithOutputs, StorePath> StorePathWithOutputs::tryFromBuildableReq(const BuildableReq & p)
{
    return std::visit(overloaded {
        [&](BuildableOpaque bo) -> std::variant<StorePathWithOutputs, StorePath> {
            if (bo.path.isDerivation()) {
                // drv path gets interpreted as "build", not "get drv file itself"
                return bo.path;
            }
            return StorePathWithOutputs { bo.path };
        },
        [&](BuildableReqFromDrv bfd) -> std::variant<StorePathWithOutputs, StorePath> {
            return StorePathWithOutputs { bfd.drvPath, bfd.outputs };
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

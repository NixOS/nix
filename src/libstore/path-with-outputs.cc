#include "store-api.hh"

namespace nix {

std::string StorePathWithOutputs::to_string(const Store & store) const
{
    return outputs.empty()
        ? store.printStorePath(path)
        : store.printStorePath(path) + "!" + concatStringsSep(",", outputs);
}


std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? std::make_pair(s, std::set<string>())
        : std::make_pair(((std::string_view) s).substr(0, n),
            tokenizeString<std::set<string>>(((std::string_view) s).substr(n + 1), ","));
}


StorePathWithOutputs Store::parsePathWithOutputs(const std::string & s)
{
    auto [path, outputs] = nix::parsePathWithOutputs(s);
    return {parseStorePath(path), std::move(outputs)};
}


StorePathWithOutputs Store::followLinksToStorePathWithOutputs(std::string_view path) const
{
    auto [path2, outputs] = nix::parsePathWithOutputs(path);
    return StorePathWithOutputs { followLinksToStorePath(path2), std::move(outputs) };
}

}

#include "fetchers.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

std::unique_ptr<std::vector<std::unique_ptr<InputScheme>>> inputSchemes = nullptr;

void registerInputScheme(std::unique_ptr<InputScheme> && inputScheme)
{
    if (!inputSchemes) inputSchemes = std::make_unique<std::vector<std::unique_ptr<InputScheme>>>();
    inputSchemes->push_back(std::move(inputScheme));
}

std::unique_ptr<Input> inputFromURL(const ParsedURL & url)
{
    for (auto & inputScheme : *inputSchemes) {
        auto res = inputScheme->inputFromURL(url);
        if (res) return res;
    }
    throw Error("input '%s' is unsupported", url.url);
}

std::unique_ptr<Input> inputFromURL(const std::string & url)
{
    return inputFromURL(parseURL(url));
}

std::unique_ptr<Input> inputFromAttrs(const Attrs & attrs)
{
    auto attrs2(attrs);
    attrs2.erase("narHash");
    for (auto & inputScheme : *inputSchemes) {
        auto res = inputScheme->inputFromAttrs(attrs2);
        if (res) {
            if (auto narHash = maybeGetStrAttr(attrs, "narHash"))
                // FIXME: require SRI hash.
                res->narHash = newHashAllowEmpty(*narHash, htUnknown);
            return res;
        }
    }
    throw Error("input '%s' is unsupported", attrsToJson(attrs));
}

Attrs Input::toAttrs() const
{
    auto attrs = toAttrsInternal();
    if (narHash)
        attrs.emplace("narHash", narHash->to_string(SRI, true));
    attrs.emplace("type", type());
    return attrs;
}

std::pair<Tree, std::shared_ptr<const Input>> Input::fetchTree(ref<Store> store) const
{
    auto [tree, input] = fetchTreeInternal(store);

    if (tree.actualPath == "")
        tree.actualPath = store->toRealPath(tree.storePath);

    if (!tree.info.narHash)
        tree.info.narHash = store->queryPathInfo(tree.storePath)->narHash;

    if (input->narHash)
        assert(input->narHash == tree.info.narHash);

    if (narHash && narHash != input->narHash)
        throw Error("NAR hash mismatch in input '%s' (%s), expected '%s', got '%s'",
            to_string(), tree.actualPath, narHash->to_string(SRI, true), input->narHash->to_string(SRI, true));

    return {std::move(tree), input};
}

}

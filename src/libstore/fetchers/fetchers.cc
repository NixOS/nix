#include "fetchers.hh"
#include "parse.hh"
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

std::unique_ptr<Input> inputFromAttrs(const Input::Attrs & attrs)
{
    for (auto & inputScheme : *inputSchemes) {
        auto res = inputScheme->inputFromAttrs(attrs);
        if (res) return res;
    }
    throw Error("input '%s' is unsupported", attrsToJson(attrs));
}

nlohmann::json attrsToJson(const fetchers::Input::Attrs & attrs)
{
    nlohmann::json json;
    for (auto & attr : attrs) {
        if (auto v = std::get_if<int64_t>(&attr.second)) {
            json[attr.first] = *v;
        } else if (auto v = std::get_if<std::string>(&attr.second)) {
            json[attr.first] = *v;
        } else abort();
    }
    return json;
}

Input::Attrs Input::toAttrs() const
{
    auto attrs = toAttrsInternal();
    if (narHash)
        attrs.emplace("narHash", narHash->to_string(SRI));
    attrs.emplace("type", type());
    return attrs;
}

std::optional<std::string> maybeGetStrAttr(const Input::Attrs & attrs, const std::string & name)
{
    auto i = attrs.find(name);
    if (i == attrs.end()) return {};
    if (auto v = std::get_if<std::string>(&i->second))
        return *v;
    throw Error("input attribute '%s' is not a string", name);
}

std::string getStrAttr(const Input::Attrs & attrs, const std::string & name)
{
    auto s = maybeGetStrAttr(attrs, name);
    if (!s)
        throw Error("input attribute '%s' is missing", name);
    return *s;
}

std::pair<Tree, std::shared_ptr<const Input>> Input::fetchTree(ref<Store> store) const
{
    auto [tree, input] = fetchTreeInternal(store);

    if (tree.actualPath == "")
        tree.actualPath = store->toRealPath(store->printStorePath(tree.storePath));

    if (!tree.info.narHash)
        tree.info.narHash = store->queryPathInfo(tree.storePath)->narHash;

    if (input->narHash)
        assert(input->narHash == tree.info.narHash);

    if (narHash && narHash != input->narHash)
        throw Error("NAR hash mismatch in input '%s', expected '%s', got '%s'",
            to_string(), narHash->to_string(SRI), input->narHash->to_string(SRI));

    return {std::move(tree), input};
}

std::shared_ptr<const Input> Input::applyOverrides(
    std::optional<std::string> ref,
    std::optional<Hash> rev) const
{
    if (ref)
        throw Error("don't know how to apply '%s' to '%s'", *ref, to_string());
    if (rev)
        throw Error("don't know how to apply '%s' to '%s'", rev->to_string(Base16, false), to_string());
    return shared_from_this();
}

}

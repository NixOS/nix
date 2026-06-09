#include "nix/fetchers/attrs.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

ResolvedAttr forceAttr(const Attr & attr)
{
    return std::visit(
        overloaded{
            [](const LazyAttr & lazy) -> ResolvedAttr { return lazy->compute(); },
            [](const std::string & v) -> ResolvedAttr { return v; },
            [](uint64_t v) -> ResolvedAttr { return v; },
            [](const Explicit<bool> & v) -> ResolvedAttr { return v; },
        },
        attr);
}

Attrs jsonToAttrs(const nlohmann::json & json)
{
    Attrs attrs;

    for (auto & i : json.items()) {
        if (i.value().is_number())
            attrs.emplace(i.key(), i.value().get<uint64_t>());
        else if (i.value().is_string())
            attrs.emplace(i.key(), i.value().get<std::string>());
        else if (i.value().is_boolean())
            attrs.emplace(i.key(), Explicit<bool>{i.value().get<bool>()});
        else
            throw Error("unsupported input attribute type in lock file");
    }

    return attrs;
}

nlohmann::json attrsToJSON(const Attrs & attrs)
{
    nlohmann::json json;
    for (auto & attr : attrs) {
        auto resolved = forceAttr(attr.second);
        if (auto v = std::get_if<uint64_t>(&resolved)) {
            json[attr.first] = *v;
        } else if (auto v = std::get_if<std::string>(&resolved)) {
            json[attr.first] = *v;
        } else if (auto v = std::get_if<Explicit<bool>>(&resolved)) {
            json[attr.first] = v->t;
        } else
            unreachable();
    }
    return json;
}

std::optional<LazyAttr> maybeGetLazyAttr(const Attrs & attrs, const std::string & name)
{
    auto i = attrs.find(name);
    if (i == attrs.end())
        return {};
    if (auto v = std::get_if<LazyAttr>(&i->second))
        return *v;
    return {};
}

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name)
{
    auto i = attrs.find(name);
    if (i == attrs.end())
        return {};
    auto resolved = forceAttr(i->second);
    if (auto v = std::get_if<std::string>(&resolved))
        return *v;
    throw Error("input attribute '%s' is not a string %s", name, attrsToJSON(attrs).dump());
}

std::string getStrAttr(const Attrs & attrs, const std::string & name)
{
    auto s = maybeGetStrAttr(attrs, name);
    if (!s)
        throw Error("input attribute '%s' is missing", name);
    return *s;
}

std::optional<uint64_t> maybeGetIntAttr(const Attrs & attrs, const std::string & name)
{
    auto i = attrs.find(name);
    if (i == attrs.end())
        return {};
    auto resolved = forceAttr(i->second);
    if (auto v = std::get_if<uint64_t>(&resolved))
        return *v;
    throw Error("input attribute '%s' is not an integer", name);
}

uint64_t getIntAttr(const Attrs & attrs, const std::string & name)
{
    auto s = maybeGetIntAttr(attrs, name);
    if (!s)
        throw Error("input attribute '%s' is missing", name);
    return *s;
}

std::optional<bool> maybeGetBoolAttr(const Attrs & attrs, const std::string & name)
{
    auto i = attrs.find(name);
    if (i == attrs.end())
        return {};
    auto resolved = forceAttr(i->second);
    if (auto v = std::get_if<Explicit<bool>>(&resolved))
        return v->t;
    throw Error("input attribute '%s' is not a Boolean", name);
}

bool getBoolAttr(const Attrs & attrs, const std::string & name)
{
    auto s = maybeGetBoolAttr(attrs, name);
    if (!s)
        throw Error("input attribute '%s' is missing", name);
    return *s;
}

StringMap attrsToQuery(const Attrs & attrs)
{
    StringMap query;
    for (auto & attr : attrs) {
        auto resolved = forceAttr(attr.second);
        if (auto v = std::get_if<uint64_t>(&resolved)) {
            query.insert_or_assign(attr.first, fmt("%d", *v));
        } else if (auto v = std::get_if<std::string>(&resolved)) {
            query.insert_or_assign(attr.first, *v);
        } else if (auto v = std::get_if<Explicit<bool>>(&resolved)) {
            query.insert_or_assign(attr.first, v->t ? "1" : "0");
        } else
            unreachable();
    }
    return query;
}

Hash getRevAttr(const Attrs & attrs, const std::string & name)
{
    return Hash::parseAny(getStrAttr(attrs, name), HashAlgorithm::SHA1);
}

} // namespace nix::fetchers

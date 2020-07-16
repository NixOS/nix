#include "attrs.hh"
#include "fetchers.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

Attrs jsonToAttrs(const nlohmann::json & json)
{
    Attrs attrs;

    for (auto & i : json.items()) {
        if (i.value().is_number())
            attrs.emplace(i.key(), i.value().get<int64_t>());
        else if (i.value().is_string())
            attrs.emplace(i.key(), i.value().get<std::string>());
        else if (i.value().is_boolean())
            attrs.emplace(i.key(), i.value().get<bool>());
        else
            throw Error("unsupported input attribute type in lock file");
    }

    return attrs;
}

nlohmann::json attrsToJson(const Attrs & attrs)
{
    nlohmann::json json;
    for (auto & attr : attrs) {
        if (auto v = std::get_if<uint64_t>(&attr.second)) {
            json[attr.first] = *v;
        } else if (auto v = std::get_if<std::string>(&attr.second)) {
            json[attr.first] = *v;
        } else if (auto v = std::get_if<Explicit<bool>>(&attr.second)) {
            json[attr.first] = v->t;
        } else abort();
    }
    return json;
}

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name)
{
    auto i = attrs.find(name);
    if (i == attrs.end()) return {};
    if (auto v = std::get_if<std::string>(&i->second))
        return *v;
    throw Error("input attribute '%s' is not a string %s", name, attrsToJson(attrs).dump());
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
    if (i == attrs.end()) return {};
    if (auto v = std::get_if<uint64_t>(&i->second))
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
    if (i == attrs.end()) return {};
    if (auto v = std::get_if<Explicit<bool>>(&i->second))
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

std::map<std::string, std::string> attrsToQuery(const Attrs & attrs)
{
    std::map<std::string, std::string> query;
    for (auto & attr : attrs) {
        if (auto v = std::get_if<uint64_t>(&attr.second)) {
            query.insert_or_assign(attr.first, fmt("%d", *v));
        } else if (auto v = std::get_if<std::string>(&attr.second)) {
            query.insert_or_assign(attr.first, *v);
        } else if (auto v = std::get_if<Explicit<bool>>(&attr.second)) {
            query.insert_or_assign(attr.first, v->t ? "1" : "0");
        } else abort();
    }
    return query;
}

}

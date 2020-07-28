#pragma once

#include "types.hh"

#include <variant>

#include <nlohmann/json_fwd.hpp>

namespace nix::fetchers {

/* Wrap bools to prevent string literals (i.e. 'char *') from being
   cast to a bool in Attr. */
template<typename T>
struct Explicit {
    T t;

    bool operator ==(const Explicit<T> & other) const
    {
        return t == other.t;
    }
};

typedef std::variant<std::string, uint64_t, Explicit<bool>> Attr;
typedef std::map<std::string, Attr> Attrs;

Attrs jsonToAttrs(const nlohmann::json & json);

nlohmann::json attrsToJson(const Attrs & attrs);

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name);

std::string getStrAttr(const Attrs & attrs, const std::string & name);

std::optional<uint64_t> maybeGetIntAttr(const Attrs & attrs, const std::string & name);

uint64_t getIntAttr(const Attrs & attrs, const std::string & name);

std::optional<bool> maybeGetBoolAttr(const Attrs & attrs, const std::string & name);

bool getBoolAttr(const Attrs & attrs, const std::string & name);

std::map<std::string, std::string> attrsToQuery(const Attrs & attrs);

}

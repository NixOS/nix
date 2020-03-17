#pragma once

#include "types.hh"

#include <variant>

#include <nlohmann/json_fwd.hpp>

namespace nix::fetchers {

typedef std::variant<std::string, int64_t> Attr;
typedef std::map<std::string, Attr> Attrs;

Attrs jsonToAttrs(const nlohmann::json & json);

nlohmann::json attrsToJson(const Attrs & attrs);

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name);

std::string getStrAttr(const Attrs & attrs, const std::string & name);

std::optional<int64_t> maybeGetIntAttr(const Attrs & attrs, const std::string & name);

int64_t getIntAttr(const Attrs & attrs, const std::string & name);

}

#pragma once
///@file

#include "types.hh"
#include "hash.hh"

#include <variant>

#include <nlohmann/json_fwd.hpp>

#include <optional>

namespace nix::fetchers {

/**
 * A primitive value that can be used in a fetcher attribute.
 */
typedef std::variant<std::string, uint64_t, Explicit<bool>> Attr;

/**
 * An `Attrs` can be thought of a JSON object restricted or simplified
 * to be "flat", not containing any subcontainers (arrays or objects)
 * and also not containing any `null`s.
 */
typedef std::map<std::string, Attr> Attrs;

/**
 * A lowercase string designating the type of an `Attr`.
 *
 * Matches `builtins.typeOf` in Nix.
 */
std::string attrType(const Attr & attr);

Attrs jsonToAttrs(const nlohmann::json & json);

nlohmann::json attrsToJSON(const Attrs & attrs);

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name);

std::string getStrAttr(const Attrs & attrs, const std::string & name);

std::optional<uint64_t> maybeGetIntAttr(const Attrs & attrs, const std::string & name);

uint64_t getIntAttr(const Attrs & attrs, const std::string & name);

std::optional<bool> maybeGetBoolAttr(const Attrs & attrs, const std::string & name);

bool getBoolAttr(const Attrs & attrs, const std::string & name);

std::map<std::string, std::string> attrsToQuery(const Attrs & attrs);

}

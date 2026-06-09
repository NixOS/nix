#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/hash.hh"
#include "nix/util/ref.hh"
#include "nix/util/fun.hh"

#include <variant>

#include <nlohmann/json_fwd.hpp>

#include <optional>

namespace nix::fetchers {

/**
 * The resolved (non-lazy) subset of attribute value types.
 */
using ResolvedAttr = std::variant<std::string, uint64_t, Explicit<bool>>;

/**
 * A deferred attribute computation.  Wrapping in `ref<>` gives
 * pointer-identity equality/ordering, which is correct: two lazy
 * attrs are equal iff they are the same computation.
 */
struct LazyAttrComputation
{
    fun<ResolvedAttr()> compute;
};

using LazyAttr = ref<LazyAttrComputation>;

using Attr = std::variant<std::string, uint64_t, Explicit<bool>, LazyAttr>;

/**
 * An `Attrs` can be thought of a JSON object restricted or simplified
 * to be "flat", not containing any subcontainers (arrays or objects)
 * and also not containing any `null`s.
 */
typedef std::map<std::string, Attr> Attrs;

/**
 * Force a potentially lazy attribute to its resolved value.
 */
ResolvedAttr forceAttr(const Attr & attr);

/**
 * Retrieve an attr, but only if it's a LazyAttr.
 */
std::optional<LazyAttr> maybeGetLazyAttr(const Attrs & attrs, const std::string & name);

Attrs jsonToAttrs(const nlohmann::json & json);

nlohmann::json attrsToJSON(const Attrs & attrs);

std::optional<std::string> maybeGetStrAttr(const Attrs & attrs, const std::string & name);

std::string getStrAttr(const Attrs & attrs, const std::string & name);

std::optional<uint64_t> maybeGetIntAttr(const Attrs & attrs, const std::string & name);

uint64_t getIntAttr(const Attrs & attrs, const std::string & name);

std::optional<bool> maybeGetBoolAttr(const Attrs & attrs, const std::string & name);

bool getBoolAttr(const Attrs & attrs, const std::string & name);

StringMap attrsToQuery(const Attrs & attrs);

Hash getRevAttr(const Attrs & attrs, const std::string & name);

} // namespace nix::fetchers

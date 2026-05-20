#pragma once
///@file

#include "nix/expr/symbol-table.hh"

#include <string>
#include <variant>
#include <vector>

namespace nix {

/**
 * A path into a Nix value tree: a sequence of attribute names and list
 * indices identifying a sub-value.
 */
using ValuePath = std::vector<std::variant<Symbol, size_t>>;

/**
 * Render a `ValuePath` for use in error messages. Returns
 * `"the top-level value"` for an empty path and otherwise
 * `"the value at <segments>"` where each segment is `.<attr>` or `[<index>]`.
 */
std::string showValuePath(const SymbolTable & symbols, const ValuePath & p);

} // namespace nix

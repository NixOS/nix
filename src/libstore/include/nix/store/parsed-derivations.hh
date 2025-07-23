#pragma once
///@file

#include <nlohmann/json.hpp>

#include "nix/util/types.hh"
#include "nix/store/path.hh"

namespace nix {

class Store;
struct DerivationOptions;
struct DerivationOutput;

typedef std::map<std::string, DerivationOutput> DerivationOutputs;

struct StructuredAttrs
{
    nlohmann::json structuredAttrs;

    static std::optional<StructuredAttrs> tryParse(const StringPairs & env);

    nlohmann::json prepareStructuredAttrs(
        Store & store,
        const DerivationOptions & drvOptions,
        const StorePathSet & inputPaths,
        const DerivationOutputs & outputs) const;

    /**
     * As a convenience to bash scripts, write a shell file that
     * maps all attributes that are representable in bash -
     * namely, strings, integers, nulls, Booleans, and arrays and
     * objects consisting entirely of those values. (So nested
     * arrays or objects are not supported.)
     *
     * @param prepared This should be the result of
     * `prepareStructuredAttrs`, *not* the original `structuredAttrs`
     * field.
     */
    static std::string writeShell(const nlohmann::json & prepared);
};

}

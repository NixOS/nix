#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct DerivationOptions;

class ParsedDerivation
{
    BasicDerivation & drv;
    std::unique_ptr<nlohmann::json> structuredAttrs;

    std::optional<std::string> getStringAttr(const std::string & name) const;

    bool getBoolAttr(const std::string & name, bool def = false) const;

    std::optional<Strings> getStringsAttr(const std::string & name) const;

    std::optional<StringSet> getStringSetAttr(const std::string & name) const;

    /**
     * Only `DerivationOptions` is allowed to parse individual fields
     * from `ParsedDerivation`. This ensure that it includes all
     * derivation options, and, the likes of `LocalDerivationGoal` are
     * incapable of more ad-hoc options.
     */
    friend struct DerivationOptions;

public:

    ParsedDerivation(BasicDerivation & drv);

    ~ParsedDerivation();

    bool hasStructuredAttrs() const
    {
        return static_cast<bool>(structuredAttrs);
    }

    std::optional<nlohmann::json> prepareStructuredAttrs(Store & store, const StorePathSet & inputPaths);
};

std::string writeStructuredAttrsShell(const nlohmann::json & json);

}

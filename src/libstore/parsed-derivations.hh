#pragma once
///@file

#include "derivations.hh"
#include "store-api.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

class ParsedDerivation
{
    const StringPairs & env;
    std::unique_ptr<nlohmann::json> structuredAttrs;

public:

    ParsedDerivation(const StringPairs & env);

    ~ParsedDerivation();

    const nlohmann::json * getStructuredAttrs() const
    {
        return structuredAttrs.get();
    }

    std::optional<std::string> getStringAttr(const std::string & name) const;

    bool getBoolAttr(const std::string & name, bool def = false) const;

    std::optional<Strings> getStringsAttr(const std::string & name) const;

    std::optional<nlohmann::json>
    prepareStructuredAttrs(Store & store, const StorePathSet & inputPaths, const BasicDerivation & drv);
};

std::string writeStructuredAttrsShell(const nlohmann::json & json);

}

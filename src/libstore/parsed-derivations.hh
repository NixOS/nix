#pragma once

#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix {

class ParsedDerivation
{
    StorePath drvPath;
    BasicDerivation & drv;
    std::unique_ptr<nlohmann::json> structuredAttrs;

public:

    ParsedDerivation(const StorePath & drvPath, BasicDerivation & drv);

    ~ParsedDerivation();

    const nlohmann::json * getStructuredAttrs() const
    {
        return structuredAttrs.get();
    }

    std::optional<std::string> getStringAttr(const std::string & name) const;

    bool getBoolAttr(const std::string & name, bool def = false) const;

    std::optional<Strings> getStringsAttr(const std::string & name) const;

    StringSet getRequiredSystemFeatures() const;

    bool canBuildLocally(Store & localStore) const;

    bool willBuildLocally(Store & localStore) const;

    bool substitutesAllowed() const;
};

}

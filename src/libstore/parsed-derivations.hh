#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {

class ParsedDerivation
{
    Path drvPath;
    BasicDerivation & drv;
    std::optional<nlohmann::json> structuredAttrs;

public:

    ParsedDerivation(const Path & drvPath, BasicDerivation & drv);

    const std::optional<nlohmann::json> & getStructuredAttrs() const
    {
        return structuredAttrs;
    }

    std::optional<std::string> getStringAttr(const std::string & name) const;

    bool getBoolAttr(const std::string & name, bool def = false) const;

    std::optional<Strings> getStringsAttr(const std::string & name) const;

    StringSet getRequiredSystemFeatures() const;

    bool canBuildLocally() const;

    bool willBuildLocally() const;

    bool substitutesAllowed() const;
};

}

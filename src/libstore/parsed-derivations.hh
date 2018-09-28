#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {

class ParsedDerivation
{
    Path drvPath;
    BasicDerivation & drv;
    std::experimental::optional<nlohmann::json> structuredAttrs;

public:

    ParsedDerivation(const Path & drvPath, BasicDerivation & drv);

    const std::experimental::optional<nlohmann::json> & getStructuredAttrs() const
    {
        return structuredAttrs;
    }

    std::experimental::optional<std::string> getStringAttr(const std::string & name) const;

    bool getBoolAttr(const std::string & name, bool def = false) const;

    std::experimental::optional<Strings> getStringsAttr(const std::string & name) const;

    StringSet getRequiredSystemFeatures() const;

    bool canBuildLocally() const;

    bool willBuildLocally() const;
};

}

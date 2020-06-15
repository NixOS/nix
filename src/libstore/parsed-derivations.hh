#include "derivations.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

class ParsedDerivation
{
    StorePath drvPath;
    BasicDerivation & drv;
    std::unique_ptr<nlohmann::json> structuredAttrs;

public:

    ParsedDerivation(StorePath && drvPath, BasicDerivation & drv);

    ~ParsedDerivation();

    const nlohmann::json * getStructuredAttrs() const
    {
        return structuredAttrs.get();
    }

    std::optional<std::string> getStringAttr(std::string_view name) const;

    bool getBoolAttr(std::string_view name, bool def = false) const;

    std::optional<Strings> getStringsAttr(std::string_view name) const;

    StringSet getRequiredSystemFeatures() const;

    bool canBuildLocally() const;

    bool willBuildLocally() const;

    bool substitutesAllowed() const;
};

}

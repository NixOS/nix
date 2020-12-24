#include "realisation.hh"
#include "store-api.hh"
#include <nlohmann/json.hpp>

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const std::string &strRep) {
    size_t n = strRep.find("!");
    if (n == strRep.npos)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutput{
        .drvHash = Hash::parseAnyPrefixed(strRep.substr(0, n)),
        .outputName = strRep.substr(n+1),
    };
}

std::string DrvOutput::to_string() const {
    return strHash() + "!" + outputName;
}

nlohmann::json Realisation::toJSON() const {
    return nlohmann::json{
        {"id", id.to_string()},
        {"outPath", outPath.to_string()},
    };
}

Realisation Realisation::fromJSON(
    const nlohmann::json& json,
    const std::string& whence) {
    auto getField = [&](std::string fieldName) -> std::string {
        auto fieldIterator = json.find(fieldName);
        if (fieldIterator == json.end())
            throw Error(
                "Drv output info file '%1%' is corrupt, missing field %2%",
                whence, fieldName);
        return *fieldIterator;
    };

    return Realisation{
        .id = DrvOutput::parse(getField("id")),
        .outPath = StorePath(getField("outPath")),
    };
}

} // namespace nix

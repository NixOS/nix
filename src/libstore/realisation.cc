#include "realisation.hh"
#include "store-api.hh"

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const std::string &strRep) {
    const auto &[rawPath, outputs] = parsePathWithOutputs(strRep);
    if (outputs.size() != 1)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutput{
        .drvPath = StorePath(rawPath),
        .outputName = *outputs.begin(),
    };
}

std::string DrvOutput::to_string() const {
    return std::string(drvPath.to_string()) + "!" + outputName;
}

std::string Realisation::to_string() const {
    std::string res;

    res += "Id: " + id.to_string() + '\n';
    res += "OutPath: " + std::string(outPath.to_string()) + '\n';

    return res;
}

Realisation Realisation::parse(const std::string & s, const std::string & whence)
{
    // XXX: Copy-pasted from NarInfo::NarInfo. Should be factored out
    auto corrupt = [&]() {
        return Error("Drv output info file '%1%' is corrupt", whence);
    };

    std::optional<DrvOutput> id;
    std::optional<StorePath> outPath;

    size_t pos = 0;
    while (pos < s.size()) {

        size_t colon = s.find(':', pos);
        if (colon == std::string::npos) throw corrupt();

        std::string name(s, pos, colon - pos);

        size_t eol = s.find('\n', colon + 2);
        if (eol == std::string::npos) throw corrupt();

        std::string value(s, colon + 2, eol - colon - 2);

        if (name == "Id")
            id = DrvOutput::parse(value);

        if (name == "OutPath")
            outPath = StorePath(value);

        pos = eol + 1;
    }

    if (!outPath) corrupt();
    if (!id) corrupt();
    return Realisation {
        .id = *id,
        .outPath = *outPath,
    };
}

} // namespace nix

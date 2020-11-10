#include "drv-output-info.hh"
#include "store-api.hh"

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutputId DrvOutputId::parse(const std::string &strRep) {
    const auto &[rawPath, outputs] = parsePathWithOutputs(strRep);
    if (outputs.size() != 1)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutputId{
        .drvPath = StorePath(rawPath),
        .outputName = *outputs.begin(),
    };
}

std::string DrvOutputId::to_string() const {
    return std::string(drvPath.to_string()) + "!" + outputName;
}

DrvInput DrvInput::parse(const std::string & strRep)
{
    try {
        return DrvInput(DrvOutputId::parse(strRep));
    } catch (InvalidDerivationOutputId) {
        return DrvInput(StorePath(strRep));
    }
}

std::string DrvInput::to_string() const {
    return std::visit(
        overloaded{
            [&](StorePath p) -> std::string { return std::string(p.to_string()); },
            [&](DrvOutputId id) -> std::string { return id.to_string(); },
        },
        static_cast<RawDrvInput>(*this));
}

std::string DrvOutputInfo::to_string() const {
    std::string res;

    res += "OutPath: " + std::string(outPath.to_string()) + '\n';
    std::set<std::string> rawDependencies;
    if (!dependencies.empty()) {
        for (auto & dep : dependencies)
            rawDependencies.insert(dep.to_string());
        res += "Dependencies: " + concatStringsSep(" ", rawDependencies) + '\n';
    }

    return res;
}

DrvOutputInfo DrvOutputInfo::parse(const std::string & s, const std::string & whence)
{
    // XXX: Copy-pasted from NarInfo::NarInfo. Should be factored out
    auto corrupt = [&]() {
        return Error("Drv output info file '%1%' is corrupt", whence);
    };

    std::optional<StorePath> outPath;
    std::set<DrvInput> dependencies;

    size_t pos = 0;
    while (pos < s.size()) {

        size_t colon = s.find(':', pos);
        if (colon == std::string::npos) throw corrupt();

        std::string name(s, pos, colon - pos);

        size_t eol = s.find('\n', colon + 2);
        if (eol == std::string::npos) throw corrupt();

        std::string value(s, colon + 2, eol - colon - 2);

        if (name == "OutPath")
            outPath = StorePath(value);

        if (name == "Dependencies")
            for (auto& rawDep : tokenizeString<Strings>(value, " "))
                dependencies.insert(DrvInput::parse(rawDep));

        pos = eol + 1;
    }

    if (!outPath) corrupt();
    return DrvOutputInfo { .outPath = *outPath, .dependencies = dependencies };
}

} // namespace nix

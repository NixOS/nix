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
        {"signatures", signatures},
    };
}

Realisation Realisation::fromJSON(
    const nlohmann::json& json,
    const std::string& whence) {
    auto getOptionalField = [&](std::string fieldName) -> std::optional<std::string> {
        auto fieldIterator = json.find(fieldName);
        if (fieldIterator == json.end())
            return std::nullopt;
        return *fieldIterator;
    };
    auto getField = [&](std::string fieldName) -> std::string {
        if (auto field = getOptionalField(fieldName))
            return *field;
        else
            throw Error(
                "Drv output info file '%1%' is corrupt, missing field %2%",
                whence, fieldName);
    };

    StringSet signatures;
    if (auto signaturesIterator = json.find("signatures"); signaturesIterator != json.end())
        signatures.insert(signaturesIterator->begin(), signaturesIterator->end());

    return Realisation{
        .id = DrvOutput::parse(getField("id")),
        .outPath = StorePath(getField("outPath")),
        .signatures = signatures,
    };
}

std::string Realisation::fingerprint() const
{
    auto serialized = toJSON();
    serialized.erase("signatures");
    return serialized.dump();
}

void Realisation::sign(const SecretKey & secretKey)
{
    signatures.insert(secretKey.signDetached(fingerprint()));
}

bool Realisation::checkSignature(const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(), sig, publicKeys);
}

size_t Realisation::checkSignatures(const PublicKeys & publicKeys) const
{
    // FIXME: Maybe we should return `maxSigs` if the realisation corresponds to
    // an input-addressed one − because in that case the drv is enough to check
    // it − but we can't know that here.

    size_t good = 0;
    for (auto & sig : signatures)
        if (checkSignature(publicKeys, sig))
            good++;
    return good;
}

StorePath RealisedPath::path() const {
    return std::visit([](auto && arg) { return arg.getPath(); }, raw);
}

void RealisedPath::closure(
    Store& store,
    const RealisedPath::Set& startPaths,
    RealisedPath::Set& ret)
{
    // FIXME: This only builds the store-path closure, not the real realisation
    // closure
    StorePathSet initialStorePaths, pathsClosure;
    for (auto& path : startPaths)
        initialStorePaths.insert(path.path());
    store.computeFSClosure(initialStorePaths, pathsClosure);
    ret.insert(startPaths.begin(), startPaths.end());
    ret.insert(pathsClosure.begin(), pathsClosure.end());
}

void RealisedPath::closure(Store& store, RealisedPath::Set & ret) const
{
    RealisedPath::closure(store, {*this}, ret);
}

RealisedPath::Set RealisedPath::closure(Store& store) const
{
    RealisedPath::Set ret;
    closure(store, ret);
    return ret;
}

} // namespace nix

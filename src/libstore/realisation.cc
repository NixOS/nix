#include "realisation.hh"
#include "store-api.hh"
#include "closure.hh"
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

std::set<Realisation> Realisation::closure(Store & store, const std::set<Realisation> & startOutputs)
{
    std::set<Realisation> res;
    Realisation::closure(store, startOutputs, res);
    return res;
}

void Realisation::closure(Store & store, const std::set<Realisation> & startOutputs, std::set<Realisation> & res)
{
    auto getDeps = [&](const Realisation& current) -> std::set<Realisation> {
        std::set<Realisation> res;
        for (auto& [currentDep, _] : current.dependentRealisations) {
            if (auto currentRealisation = store.queryRealisation(currentDep))
                res.insert(*currentRealisation);
            else
                throw Error(
                    "Unrealised derivation '%s'", currentDep.to_string());
        }
        return res;
    };

    computeClosure<Realisation>(
        startOutputs, res,
        [&](const Realisation& current,
            std::function<void(std::promise<std::set<Realisation>>&)>
                processEdges) {
            std::promise<std::set<Realisation>> promise;
            try {
                auto res = getDeps(current);
                promise.set_value(res);
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
            return processEdges(promise);
        });
}

nlohmann::json Realisation::toJSON() const {
    auto jsonDependentRealisations = nlohmann::json::object();
    for (auto & [depId, depOutPath] : dependentRealisations)
        jsonDependentRealisations.emplace(depId.to_string(), depOutPath.to_string());
    return nlohmann::json{
        {"id", id.to_string()},
        {"outPath", outPath.to_string()},
        {"signatures", signatures},
        {"dependentRealisations", jsonDependentRealisations},
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

    std::map <DrvOutput, StorePath> dependentRealisations;
    if (auto jsonDependencies = json.find("dependentRealisations"); jsonDependencies != json.end())
        for (auto & [jsonDepId, jsonDepOutPath] : jsonDependencies->get<std::map<std::string, std::string>>())
            dependentRealisations.insert({DrvOutput::parse(jsonDepId), StorePath(jsonDepOutPath)});

    return Realisation{
        .id = DrvOutput::parse(getField("id")),
        .outPath = StorePath(getField("outPath")),
        .signatures = signatures,
        .dependentRealisations = dependentRealisations,
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

bool Realisation::isCompatibleWith(const Realisation & other) const
{
    assert (id == other.id);
    if (outPath == other.outPath) {
        if (dependentRealisations.empty() != other.dependentRealisations.empty()) {
            warn(
                "Encountered a realisation for '%s' with an empty set of "
                "dependencies. This is likely an artifact from an older Nix. "
                "I’ll try to fix the realisation if I can",
                id.to_string());
            return true;
        } else if (dependentRealisations == other.dependentRealisations) {
            return true;
        }
    }
    return false;
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

#include "nix/store/realisation.hh"
#include "nix/store/store-api.hh"
#include "nix/util/closure.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"
#include <nlohmann/json.hpp>

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const std::string & strRep)
{
    size_t n = strRep.find("!");
    if (n == strRep.npos)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutput{
        .drvHash = Hash::parseAnyPrefixed(strRep.substr(0, n)),
        .outputName = strRep.substr(n + 1),
    };
}

std::string DrvOutput::to_string() const
{
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
    auto getDeps = [&](const Realisation & current) -> std::set<Realisation> {
        std::set<Realisation> res;
        for (auto & [currentDep, _] : current.dependentRealisations) {
            if (auto currentRealisation = store.queryRealisation(currentDep))
                res.insert({*currentRealisation, currentDep});
            else
                throw Error("Unrealised derivation '%s'", currentDep.to_string());
        }
        return res;
    };

    computeClosure<Realisation>(
        startOutputs,
        res,
        [&](const Realisation & current, std::function<void(std::promise<std::set<Realisation>> &)> processEdges) {
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

std::string UnkeyedRealisation::fingerprint(const DrvOutput & key) const
{
    nlohmann::json serialized = Realisation{*this, key};
    serialized.erase("signatures");
    return serialized.dump();
}

void UnkeyedRealisation::sign(const DrvOutput & key, const Signer & signer)
{
    signatures.insert(signer.signDetached(fingerprint(key)));
}

bool UnkeyedRealisation::checkSignature(
    const DrvOutput & key, const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(key), sig, publicKeys);
}

size_t UnkeyedRealisation::checkSignatures(const DrvOutput & key, const PublicKeys & publicKeys) const
{
    // FIXME: Maybe we should return `maxSigs` if the realisation corresponds to
    // an input-addressed one − because in that case the drv is enough to check
    // it − but we can't know that here.

    size_t good = 0;
    for (auto & sig : signatures)
        if (checkSignature(key, publicKeys, sig))
            good++;
    return good;
}

const StorePath & RealisedPath::path() const &
{
    return std::visit([](auto & arg) -> auto & { return arg.getPath(); }, raw);
}

bool Realisation::isCompatibleWith(const UnkeyedRealisation & other) const
{
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

void RealisedPath::closure(Store & store, const RealisedPath::Set & startPaths, RealisedPath::Set & ret)
{
    // FIXME: This only builds the store-path closure, not the real realisation
    // closure
    StorePathSet initialStorePaths, pathsClosure;
    for (auto & path : startPaths)
        initialStorePaths.insert(path.path());
    store.computeFSClosure(initialStorePaths, pathsClosure);
    ret.insert(startPaths.begin(), startPaths.end());
    ret.insert(pathsClosure.begin(), pathsClosure.end());
}

void RealisedPath::closure(Store & store, RealisedPath::Set & ret) const
{
    RealisedPath::closure(store, {*this}, ret);
}

RealisedPath::Set RealisedPath::closure(Store & store) const
{
    RealisedPath::Set ret;
    closure(store, ret);
    return ret;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

DrvOutput adl_serializer<DrvOutput>::from_json(const json & json)
{
    return DrvOutput::parse(getString(json));
}

void adl_serializer<DrvOutput>::to_json(json & json, const DrvOutput & drvOutput)
{
    json = drvOutput.to_string();
}

UnkeyedRealisation adl_serializer<UnkeyedRealisation>::from_json(const json & json0)
{
    auto json = getObject(json0);

    StringSet signatures;
    if (auto signaturesOpt = optionalValueAt(json, "signatures"))
        signatures = *signaturesOpt;

    std::map<DrvOutput, StorePath> dependentRealisations;
    if (auto jsonDependencies = optionalValueAt(json, "dependentRealisations"))
        for (auto & [jsonDepId, jsonDepOutPath] : getObject(*jsonDependencies))
            dependentRealisations.insert({DrvOutput::parse(jsonDepId), jsonDepOutPath});

    return UnkeyedRealisation{
        .outPath = valueAt(json, "outPath"),
        .signatures = signatures,
        .dependentRealisations = dependentRealisations,
    };
}

void adl_serializer<UnkeyedRealisation>::to_json(json & json, const UnkeyedRealisation & r)
{
    auto jsonDependentRealisations = nlohmann::json::object();
    for (auto & [depId, depOutPath] : r.dependentRealisations)
        jsonDependentRealisations.emplace(depId.to_string(), depOutPath);
    json = {
        {"outPath", r.outPath},
        {"signatures", r.signatures},
        {"dependentRealisations", jsonDependentRealisations},
    };
}

Realisation adl_serializer<Realisation>::from_json(const json & json0)
{
    auto json = getObject(json0);

    return Realisation{
        static_cast<UnkeyedRealisation>(json0),
        valueAt(json, "id"),
    };
}

void adl_serializer<Realisation>::to_json(json & json, const Realisation & r)
{
    json = static_cast<const UnkeyedRealisation &>(r);
    json["id"] = r.id;
}

} // namespace nlohmann

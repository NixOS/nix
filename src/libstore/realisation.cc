#include "nix/store/realisation.hh"
#include "nix/store/store-api.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const StoreDirConfig & store, std::string_view s)
{
    size_t n = s.rfind('^');
    if (n == s.npos)
        throw InvalidDerivationOutputId("Invalid derivation output id '%s': missing '^'", s);
    return DrvOutput{
        .drvPath = store.parseStorePath(s.substr(0, n)),
        .outputName = OutputName{s.substr(n + 1)},
    };
}

std::string DrvOutput::render(const StoreDirConfig & store) const
{
    return std::string(store.printStorePath(drvPath)) + "^" + outputName;
}

std::string DrvOutput::to_string() const
{
    return std::string(drvPath.to_string()) + "^" + outputName;
}

nlohmann::json DrvOutput::toJSON(const StoreDirConfig & store) const
{
    return nlohmann::json{
        {"drvPath", store.printStorePath(drvPath)},
        {"outputName", outputName},
    };
}

DrvOutput DrvOutput::fromJSON(const StoreDirConfig & store, const nlohmann::json & json)
{
    auto obj = getObject(json);

    return {
        .drvPath = store.parseStorePath(getString(valueAt(obj, "drvPath"))),
        .outputName = getString(valueAt(obj, "outputName")),
    };
}

nlohmann::json UnkeyedRealisation::toJSON(const StoreDirConfig & store) const
{
    return nlohmann::json{
        {"outPath", store.printStorePath(outPath)},
        {"signatures", signatures},
    };
}

UnkeyedRealisation UnkeyedRealisation::fromJSON(const StoreDirConfig & store, const nlohmann::json & json)
{
    auto obj = getObject(json);

    StringSet signatures;
    if (auto * signaturesJson = get(obj, "signatures"))
        signatures = getStringSet(*signaturesJson);

    return {
        .outPath = store.parseStorePath(getString(valueAt(obj, "outPath"))),
        .signatures = signatures,
    };
}

nlohmann::json Realisation::toJSON(const StoreDirConfig & store) const
{
    return nlohmann::json{
        {"key", id.toJSON(store)},
        {"value", static_cast<const UnkeyedRealisation &>(*this).toJSON(store)},
    };
}

Realisation Realisation::fromJSON(const StoreDirConfig & store, const nlohmann::json & json)
{
    auto obj = getObject(json);

    return {
        UnkeyedRealisation::fromJSON(store, valueAt(obj, "key")),
        DrvOutput::fromJSON(store, valueAt(obj, "value")),
    };
}

std::string UnkeyedRealisation::fingerprint(const StoreDirConfig & store, const DrvOutput & key) const
{
    auto serialised = Realisation{*this, key}.toJSON(store);
    auto value = serialised.find("value");
    assert(value != serialised.end());
    value->erase("signatures");
    return serialised.dump();
}

void UnkeyedRealisation::sign(const StoreDirConfig & store, const DrvOutput & key, const Signer & signer)
{
    signatures.insert(signer.signDetached(fingerprint(store, key)));
}

bool UnkeyedRealisation::checkSignature(
    const StoreDirConfig & store, const DrvOutput & key, const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(store, key), sig, publicKeys);
}

size_t UnkeyedRealisation::checkSignatures(
    const StoreDirConfig & store, const DrvOutput & key, const PublicKeys & publicKeys) const
{
    // FIXME: Maybe we should return `maxSigs` if the realisation corresponds to
    // an input-addressed one − because in that case the drv is enough to check
    // it − but we can't know that here.

    size_t good = 0;
    for (auto & sig : signatures)
        if (checkSignature(store, key, publicKeys, sig))
            good++;
    return good;
}

SingleDrvOutputs filterDrvOutputs(const OutputsSpec & wanted, SingleDrvOutputs && outputs)
{
    SingleDrvOutputs ret = std::move(outputs);
    for (auto it = ret.begin(); it != ret.end();) {
        if (!wanted.contains(it->first))
            it = ret.erase(it);
        else
            ++it;
    }
    return ret;
}

const StorePath & RealisedPath::path() const
{
    return std::visit([](auto && arg) -> auto & { return arg.getPath(); }, raw);
}

MissingRealisation::MissingRealisation(
    const StoreDirConfig & store, const StorePath & drvPath, const OutputName & outputName)
    : Error("cannot operate on output '%s' of the unbuilt derivation '%s'", outputName, store.printStorePath(drvPath))
{
}

MissingRealisation::MissingRealisation(
    const StoreDirConfig & store,
    const SingleDerivedPath & drvPath,
    const StorePath & drvPathResolved,
    const OutputName & outputName)
    : MissingRealisation{store, drvPathResolved, outputName}
{
    addTrace({}, "looking up realisation for derivation '%s'", drvPath.to_string(store));
}

} // namespace nix

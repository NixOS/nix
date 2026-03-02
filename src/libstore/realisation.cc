#include "nix/store/realisation.hh"
#include "nix/store/store-api.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"
#include <nlohmann/json.hpp>

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

std::string UnkeyedRealisation::fingerprint(const DrvOutput & key) const
{
    auto serialised = static_cast<nlohmann::json>(Realisation{*this, key});
    auto value = serialised.find("value");
    assert(value != serialised.end());
    value->erase("signatures");
    return serialised.dump();
}

void UnkeyedRealisation::sign(const DrvOutput & key, const Signer & signer)
{
    signatures.insert(signer.signDetached(fingerprint(key)));
}

bool UnkeyedRealisation::checkSignature(
    const DrvOutput & key, const PublicKeys & publicKeys, const Signature & sig) const
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

MissingRealisation::MissingRealisation(
    const StoreDirConfig & store, const StorePath & drvPath, const OutputName & outputName)
    : CloneableError(
          "cannot operate on output '%s' of the unbuilt derivation '%s'", outputName, store.printStorePath(drvPath))
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

namespace nlohmann {

using namespace nix;

DrvOutput adl_serializer<DrvOutput>::from_json(const json & json)
{
    auto obj = getObject(json);

    return {
        .drvPath = valueAt(obj, "drvPath"),
        .outputName = getString(valueAt(obj, "outputName")),
    };
}

void adl_serializer<DrvOutput>::to_json(json & json, const DrvOutput & drvOutput)
{
    json = {
        {"drvPath", drvOutput.drvPath},
        {"outputName", drvOutput.outputName},
    };
}

UnkeyedRealisation adl_serializer<UnkeyedRealisation>::from_json(const json & json0)
{
    auto json = getObject(json0);

    return UnkeyedRealisation{
        .outPath = valueAt(json, "outPath"),
        .signatures = [&] -> std::set<Signature> {
            if (auto signaturesOpt = optionalValueAt(json, "signatures"))
                return *signaturesOpt;
            return {};
        }(),
    };
}

void adl_serializer<UnkeyedRealisation>::to_json(json & json, const UnkeyedRealisation & r)
{
    json = {
        {"outPath", r.outPath},
        {"signatures", r.signatures},
    };
}

Realisation adl_serializer<Realisation>::from_json(const json & json)
{
    auto obj = getObject(json);

    return {
        static_cast<UnkeyedRealisation>(valueAt(obj, "value")),
        static_cast<DrvOutput>(valueAt(obj, "key")),
    };
}

void adl_serializer<Realisation>::to_json(json & json, const Realisation & r)
{
    json = {
        {"key", r.id},
        {"value", static_cast<const UnkeyedRealisation &>(r)},
    };
}

} // namespace nlohmann

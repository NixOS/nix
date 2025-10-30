#include <nlohmann/json.hpp>

#include "nix/store/path-info.hh"
#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/comparator.hh"
#include "nix/util/strings.hh"

namespace nix {

GENERATE_CMP_EXT(
    ,
    std::weak_ordering,
    UnkeyedValidPathInfo,
    me->deriver,
    me->narHash,
    me->references,
    me->registrationTime,
    me->narSize,
    // me->id,
    me->ultimate,
    me->sigs,
    me->ca);

std::string ValidPathInfo::fingerprint(const StoreDirConfig & store) const
{
    if (narSize == 0)
        throw Error(
            "cannot calculate fingerprint of path '%s' because its size is not known", store.printStorePath(path));
    return "1;" + store.printStorePath(path) + ";" + narHash.to_string(HashFormat::Nix32, true) + ";"
           + std::to_string(narSize) + ";" + concatStringsSep(",", store.printStorePathSet(references));
}

void ValidPathInfo::sign(const Store & store, const Signer & signer)
{
    sigs.insert(signer.signDetached(fingerprint(store)));
}

void ValidPathInfo::sign(const Store & store, const std::vector<std::unique_ptr<Signer>> & signers)
{
    auto fingerprint = this->fingerprint(store);
    for (auto & signer : signers) {
        sigs.insert(signer->signDetached(fingerprint));
    }
}

std::optional<ContentAddressWithReferences> ValidPathInfo::contentAddressWithReferences() const
{
    if (!ca)
        return std::nullopt;

    switch (ca->method.raw) {
    case ContentAddressMethod::Raw::Text: {
        assert(references.count(path) == 0);
        return TextInfo{
            .hash = ca->hash,
            .references = references,
        };
    }

    case ContentAddressMethod::Raw::Flat:
    case ContentAddressMethod::Raw::NixArchive:
    case ContentAddressMethod::Raw::Git:
    default: {
        auto refs = references;
        bool hasSelfReference = false;
        if (refs.count(path)) {
            hasSelfReference = true;
            refs.erase(path);
        }
        return FixedOutputInfo{
            .method = ca->method.getFileIngestionMethod(),
            .hash = ca->hash,
            .references =
                {
                    .others = std::move(refs),
                    .self = hasSelfReference,
                },
        };
    }
    }
}

bool ValidPathInfo::isContentAddressed(const StoreDirConfig & store) const
{
    auto fullCaOpt = contentAddressWithReferences();

    if (!fullCaOpt)
        return false;

    auto caPath = store.makeFixedOutputPathFromCA(path.name(), *fullCaOpt);

    bool res = caPath == path;

    if (!res)
        printError("warning: path '%s' claims to be content-addressed but isn't", store.printStorePath(path));

    return res;
}

size_t ValidPathInfo::checkSignatures(const StoreDirConfig & store, const PublicKeys & publicKeys) const
{
    if (isContentAddressed(store))
        return maxSigs;

    size_t good = 0;
    for (auto & sig : sigs)
        if (checkSignature(store, publicKeys, sig))
            good++;
    return good;
}

bool ValidPathInfo::checkSignature(
    const StoreDirConfig & store, const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(store), sig, publicKeys);
}

Strings ValidPathInfo::shortRefs() const
{
    Strings refs;
    for (auto & r : references)
        refs.push_back(std::string(r.to_string()));
    return refs;
}

ValidPathInfo ValidPathInfo::makeFromCA(
    const StoreDirConfig & store, std::string_view name, ContentAddressWithReferences && ca, Hash narHash)
{
    ValidPathInfo res{
        store.makeFixedOutputPathFromCA(name, ca),
        narHash,
    };
    res.ca = ContentAddress{
        .method = ca.getMethod(),
        .hash = ca.getHash(),
    };
    res.references = std::visit(
        overloaded{
            [&](TextInfo && ti) { return std::move(ti.references); },
            [&](FixedOutputInfo && foi) {
                auto references = std::move(foi.references.others);
                if (foi.references.self)
                    references.insert(res.path);
                return references;
            },
        },
        std::move(ca).raw);
    return res;
}

nlohmann::json UnkeyedValidPathInfo::toJSON(const StoreDirConfig * store, bool includeImpureInfo) const
{
    using nlohmann::json;

    auto jsonObject = json::object();

    /* Back-compat hack, if we are passing a `StoreDirConfig`, do SRI,
       which `nix path-info` has always down. Otherwise, use the new
       cannonical JSON serialization for `Hash`. */
    jsonObject["narHash"] =
        store ? static_cast<json>(narHash.to_string(HashFormat::SRI, true)) : static_cast<json>(narHash);
    jsonObject["narSize"] = narSize;

    {
        auto & jsonRefs = jsonObject["references"] = json::array();
        for (auto & ref : references)
            jsonRefs.emplace_back(store ? static_cast<json>(store->printStorePath(ref)) : static_cast<json>(ref));
    }

    jsonObject["ca"] = ca ? (store ? static_cast<json>(renderContentAddress(*ca)) : static_cast<json>(*ca))
                          : static_cast<json>(nullptr);

    if (includeImpureInfo) {
        jsonObject["deriver"] = deriver ? (store ? static_cast<json>(std::optional{store->printStorePath(*deriver)})
                                                 : static_cast<json>(std::optional{*deriver}))
                                        : static_cast<json>(std::optional<StorePath>{});

        jsonObject["registrationTime"] = registrationTime ? (std::optional{registrationTime}) : std::nullopt;

        jsonObject["ultimate"] = ultimate;

        auto & sigsObj = jsonObject["signatures"] = json::array();
        for (auto & sig : sigs)
            sigsObj.push_back(sig);
    }

    return jsonObject;
}

UnkeyedValidPathInfo UnkeyedValidPathInfo::fromJSON(const StoreDirConfig * store, const nlohmann::json & _json)
{
    UnkeyedValidPathInfo res{
        Hash(Hash::dummy),
    };

    auto & json = getObject(_json);
    res.narHash = [&] {
        auto & j = valueAt(json, "narHash");
        return store ? Hash::parseAny(getString(j), std::nullopt) : static_cast<Hash>(j);
    }();
    res.narSize = getUnsigned(valueAt(json, "narSize"));

    try {
        auto references = getStringList(valueAt(json, "references"));
        for (auto & input : references)
            res.references.insert(store ? store->parseStorePath(getString(input)) : static_cast<StorePath>(input));
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'references'");
        throw;
    }

    // New format as this as nullable but mandatory field; handling
    // missing is for back-compat.
    if (json.contains("ca"))
        if (auto * rawCa = getNullable(valueAt(json, "ca")))
            res.ca = store ? ContentAddress::parse(getString(*rawCa)) : static_cast<ContentAddress>(*rawCa);

    if (json.contains("deriver"))
        if (auto * rawDeriver = getNullable(valueAt(json, "deriver")))
            res.deriver = store ? store->parseStorePath(getString(*rawDeriver)) : static_cast<StorePath>(*rawDeriver);

    if (json.contains("registrationTime"))
        if (auto * rawRegistrationTime = getNullable(valueAt(json, "registrationTime")))
            res.registrationTime = getInteger<time_t>(*rawRegistrationTime);

    if (json.contains("ultimate"))
        res.ultimate = getBoolean(valueAt(json, "ultimate"));

    if (json.contains("signatures"))
        res.sigs = getStringSet(valueAt(json, "signatures"));

    return res;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

UnkeyedValidPathInfo adl_serializer<UnkeyedValidPathInfo>::from_json(const json & json)
{
    return UnkeyedValidPathInfo::fromJSON(nullptr, json);
}

void adl_serializer<UnkeyedValidPathInfo>::to_json(json & json, const UnkeyedValidPathInfo & c)
{
    json = c.toJSON(nullptr, true);
}

} // namespace nlohmann

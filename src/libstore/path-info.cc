#include <nlohmann/json.hpp>

#include "nix/store/path-info.hh"
#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/comparator.hh"
#include "nix/util/strings.hh"

namespace nix {

PathInfoJsonFormat parsePathInfoJsonFormat(uint64_t version)
{
    switch (version) {
    case 1:
        return PathInfoJsonFormat::V1;
    case 2:
        return PathInfoJsonFormat::V2;
    case 3:
        return PathInfoJsonFormat::V3;
    default:
        throw Error("unsupported path info JSON format version %d; supported versions are 1, 2 and 3", version);
    }
}

UnkeyedValidPathInfo::UnkeyedValidPathInfo(const StoreDirConfig & store, Hash narHash)
    : UnkeyedValidPathInfo{store.storeDir, narHash}
{
}

GENERATE_CMP_EXT(
    ,
    std::weak_ordering,
    UnkeyedValidPathInfo,
    me->storeDir,
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
    const StoreDirConfig & store, const PublicKeys & publicKeys, const Signature & sig) const
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
        UnkeyedValidPathInfo(store, narHash),
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

nlohmann::json
UnkeyedValidPathInfo::toJSON(const StoreDirConfig * store, bool includeImpureInfo, PathInfoJsonFormat format) const
{
    using nlohmann::json;

    if (format == PathInfoJsonFormat::V1)
        assert(store);

    auto jsonObject = json::object();

    jsonObject["version"] = format;

    jsonObject["storeDir"] = storeDir;

    jsonObject["narHash"] = format == PathInfoJsonFormat::V1
                                ? static_cast<json>(narHash.to_string(HashFormat::SRI, true))
                                : static_cast<json>(narHash);

    jsonObject["narSize"] = narSize;

    {
        auto & jsonRefs = jsonObject["references"] = json::array();
        for (auto & ref : references)
            jsonRefs.emplace_back(
                format == PathInfoJsonFormat::V1 ? static_cast<json>(store->printStorePath(ref))
                                                 : static_cast<json>(ref));
    }

    if (format == PathInfoJsonFormat::V1)
        jsonObject["ca"] = ca ? static_cast<json>(renderContentAddress(*ca)) : static_cast<json>(nullptr);
    else
        jsonObject["ca"] = ca;

    if (includeImpureInfo) {
        if (format == PathInfoJsonFormat::V1) {
            jsonObject["deriver"] =
                deriver ? static_cast<json>(store->printStorePath(*deriver)) : static_cast<json>(nullptr);
        } else {
            jsonObject["deriver"] = deriver;
        }
        jsonObject["registrationTime"] = registrationTime ? std::optional{registrationTime} : std::nullopt;

        jsonObject["ultimate"] = ultimate;

        if (format == PathInfoJsonFormat::V3) {
            jsonObject["signatures"] = sigs;
        } else {
            auto & sigsObj = jsonObject["signatures"] = json::array();
            for (auto & sig : sigs)
                sigsObj.push_back(sig.to_string());
        }
    }

    return jsonObject;
}

UnkeyedValidPathInfo UnkeyedValidPathInfo::fromJSON(const StoreDirConfig * store, const nlohmann::json & _json)
{
    auto & json = getObject(_json);

    PathInfoJsonFormat format = PathInfoJsonFormat::V1;
    if (auto * version = optionalValueAt(json, "version"))
        format = *version;

    if (format == PathInfoJsonFormat::V1)
        assert(store);

    UnkeyedValidPathInfo res{
        [&] {
            if (auto * rawStoreDir = optionalValueAt(json, "storeDir"))
                return getString(*rawStoreDir);
            else if (format == PathInfoJsonFormat::V1)
                return store->storeDir;
            else
                throw Error("'storeDir' field is required in path info JSON format version 2");
        }(),
        [&] {
            return format == PathInfoJsonFormat::V1 ? Hash::parseSRI(getString(valueAt(json, "narHash")))
                                                    : Hash(valueAt(json, "narHash"));
        }(),
    };

    res.narSize = getUnsigned(valueAt(json, "narSize"));

    try {
        auto & references = getArray(valueAt(json, "references"));
        for (auto & input : references)
            res.references.insert(
                format == PathInfoJsonFormat::V1 ? store->parseStorePath(getString(input))
                                                 : static_cast<StorePath>(input));
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'references'");
        throw;
    }

    try {
        if (format == PathInfoJsonFormat::V1) {
            if (auto * rawCa = getNullable(valueAt(json, "ca")))
                res.ca = ContentAddress::parse(getString(*rawCa));
        } else {
            res.ca = ptrToOwned<ContentAddress>(getNullable(valueAt(json, "ca")));
        }
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'ca'");
        throw;
    }

    if (auto * rawDeriver0 = optionalValueAt(json, "deriver")) {
        if (format == PathInfoJsonFormat::V1) {
            if (auto * rawDeriver = getNullable(*rawDeriver0))
                res.deriver = store->parseStorePath(getString(*rawDeriver));
        } else {
            res.deriver = ptrToOwned<StorePath>(getNullable(*rawDeriver0));
        }
    }

    if (auto * rawRegistrationTime0 = optionalValueAt(json, "registrationTime"))
        if (auto * rawRegistrationTime = getNullable(*rawRegistrationTime0))
            res.registrationTime = getInteger<time_t>(*rawRegistrationTime);

    if (auto * rawUltimate = optionalValueAt(json, "ultimate"))
        res.ultimate = getBoolean(*rawUltimate);

    if (auto * rawSignatures = optionalValueAt(json, "signatures"))
        res.sigs = *rawSignatures;

    return res;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

PathInfoJsonFormat adl_serializer<PathInfoJsonFormat>::from_json(const json & json)
{
    return parsePathInfoJsonFormat(getUnsigned(json));
}

void adl_serializer<PathInfoJsonFormat>::to_json(json & json, const PathInfoJsonFormat & format)
{
    json = static_cast<int>(format);
}

UnkeyedValidPathInfo adl_serializer<UnkeyedValidPathInfo>::from_json(const json & json)
{
    return UnkeyedValidPathInfo::fromJSON(nullptr, json);
}

void adl_serializer<UnkeyedValidPathInfo>::to_json(json & json, const UnkeyedValidPathInfo & c)
{
    json = c.toJSON(nullptr, true, PathInfoJsonFormat::V3);
}

ValidPathInfo adl_serializer<ValidPathInfo>::from_json(const json & json0)
{
    auto json = getObject(json0);

    return ValidPathInfo{
        valueAt(json, "path"),
        adl_serializer<UnkeyedValidPathInfo>::from_json(json0),
    };
}

void adl_serializer<ValidPathInfo>::to_json(json & json, const ValidPathInfo & v)
{
    adl_serializer<UnkeyedValidPathInfo>::to_json(json, v);
    json["path"] = v.path;
}

} // namespace nlohmann

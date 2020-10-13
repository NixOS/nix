#include <nlohmann/json.hpp>

#include "args.hh"
#include "content-address.hh"
#include "split.hh"

namespace nix {

std::string FixedOutputHash::printMethodAlgo() const
{
    return makeFileIngestionPrefix(method) + printHashType(hash.type);
}

std::string makeFileIngestionPrefix(const FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    default:
        throw Error("impossible, caught both cases");
    }
}

std::string makeFixedOutputCA(FileIngestionMethod method, const Hash & hash)
{
    return "fixed:"
        + makeFileIngestionPrefix(method)
        + hash.to_string(Base32, true);
}

std::string renderContentAddress(ContentAddress ca)
{
    return std::visit(overloaded {
        [](TextHash th) {
            return "text:" + th.hash.to_string(Base32, true);
        },
        [](FixedOutputHash fsh) {
            return makeFixedOutputCA(fsh.method, fsh.hash);
        }
    }, ca);
}

std::string renderContentAddressMethod(ContentAddressMethod cam)
{
    return std::visit(overloaded {
        [](TextHashMethod &th) {
            return std::string{"text:"} + printHashType(htSHA256);
        },
        [](FixedOutputHashMethod &fshm) {
            return "fixed:" + makeFileIngestionPrefix(fshm.fileIngestionMethod) + printHashType(fshm.hashType);
        }
    }, cam);
}

/*
  Parses content address strings up to the hash.
 */
static ContentAddressMethod parseContentAddressMethodPrefix(std::string_view & rest)
{
    std::string_view wholeInput { rest };

    std::string_view prefix;
    {
        auto optPrefix = splitPrefixTo(rest, ':');
        if (!optPrefix)
            throw UsageError("not a content address because it is not in the form '<prefix>:<rest>': %s", wholeInput);
        prefix = *optPrefix;
    }

    auto parseHashType_ = [&](){
        auto hashTypeRaw = splitPrefixTo(rest, ':');
        if (!hashTypeRaw)
            throw UsageError("content address hash must be in form '<algo>:<hash>', but found: %s", wholeInput);
        HashType hashType = parseHashType(*hashTypeRaw);
        return std::move(hashType);
    };

    // Switch on prefix
    if (prefix == "text") {
        // No parsing of the ingestion method, "text" only support flat.
        HashType hashType = parseHashType_();
        if (hashType != htSHA256)
            throw Error("text content address hash should use %s, but instead uses %s",
                printHashType(htSHA256), printHashType(hashType));
        return TextHashMethod {};
    } else if (prefix == "fixed") {
        // Parse method
        auto method = FileIngestionMethod::Flat;
        if (splitPrefix(rest, "r:"))
            method = FileIngestionMethod::Recursive;
        HashType hashType = parseHashType_();
        return FixedOutputHashMethod {
            .fileIngestionMethod = method,
            .hashType = std::move(hashType),
        };
    } else
        throw UsageError("content address prefix '%s' is unrecognized. Recogonized prefixes are 'text' or 'fixed'", prefix);
}

ContentAddress parseContentAddress(std::string_view rawCa) {
    auto rest = rawCa;

    ContentAddressMethod caMethod = parseContentAddressMethodPrefix(rest);

    return std::visit(
        overloaded {
            [&](TextHashMethod thm) {
                return ContentAddress(TextHash {
                    .hash = Hash::parseNonSRIUnprefixed(rest, htSHA256)
                });
            },
            [&](FixedOutputHashMethod fohMethod) {
                return ContentAddress(FixedOutputHash {
                    .method = fohMethod.fileIngestionMethod,
                    .hash = Hash::parseNonSRIUnprefixed(rest, std::move(fohMethod.hashType)),
                });
            },
        }, caMethod);
}

ContentAddressMethod parseContentAddressMethod(std::string_view caMethod)
{
    std::string_view asPrefix {std::string{caMethod} + ":"};
    return parseContentAddressMethodPrefix(asPrefix);
}

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt)
{
    return rawCaOpt == "" ? std::optional<ContentAddress>() : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca)
{
    return ca ? renderContentAddress(*ca) : "";
}

void to_json(nlohmann::json& j, const ContentAddress & ca) {
    j = std::visit(overloaded {
        [](TextHash th) {
            return nlohmann::json {
                { "type", "text" },
                { "hash", th.hash.to_string(Base32, false) },
            };
        },
        [](FixedOutputHash foh) {
            return nlohmann::json {
                { "type", "fixed" },
                { "method", foh.method == FileIngestionMethod::Flat ? "flat" : "recursive" },
                { "algo", printHashType(foh.hash.type) },
                { "hash", foh.hash.to_string(Base32, false) },
            };
        }
    }, ca);
}

void from_json(const nlohmann::json& j, ContentAddress & ca) {
    std::string_view type = j.at("type").get<std::string_view>();
    if (type == "text") {
        ca = TextHash {
            .hash = Hash::parseNonSRIUnprefixed(j.at("hash").get<std::string_view>(), htSHA256),
        };
    } else if (type == "fixed") {
        std::string_view methodRaw = j.at("method").get<std::string_view>();
        auto method = methodRaw == "flat" ? FileIngestionMethod::Flat
            : methodRaw == "recursive" ? FileIngestionMethod::Recursive
            : throw Error("invalid file ingestion method: %s", methodRaw);
        auto hashAlgo = parseHashType(j.at("algo").get<std::string>());
        ca = FixedOutputHash {
            .method = method,
            .hash = Hash::parseNonSRIUnprefixed(j.at("hash").get<std::string_view>(), hashAlgo),
        };
    } else
        throw Error("invalid type: %s", type);
}

// Needed until https://github.com/nlohmann/json/pull/2117

void to_json(nlohmann::json& j, const std::optional<ContentAddress> & c) {
    if (!c)
        j = nullptr;
    else
        to_json(j, *c);
}

void from_json(const nlohmann::json& j, std::optional<ContentAddress> & c) {
    if (j.is_null()) {
        c = std::nullopt;
    } else {
        // Dummy value to set tag bit.
        c = TextHash { .hash = Hash { htSHA256 } };
        from_json(j, *c);
    }
}

Hash getContentAddressHash(const ContentAddress & ca)
{
    return std::visit(overloaded {
        [](TextHash th) {
            return th.hash;
        },
        [](FixedOutputHash fsh) {
            return fsh.hash;
        }
    }, ca);
}

}

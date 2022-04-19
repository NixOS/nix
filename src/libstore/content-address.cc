#include "args.hh"
#include "content-address.hh"
#include "split.hh"

namespace nix {

std::string FixedOutputHash::printMethodAlgo() const
{
    return makeFileIngestionPrefix(method) + printHashType(hash.type);
}


std::string makeFileIngestionPrefix(FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    }
    assert(false);
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
        [](TextHash & th) {
            return "text:"
                + th.hash.to_string(Base32, true);
        },
        [](FixedOutputHash & fsh) {
            return "fixed:"
                + makeFileIngestionPrefix(fsh.method)
                + fsh.hash.to_string(Base32, true);
        }
    }, ca);
}

std::string renderContentAddressMethod(ContentAddressMethod cam)
{
    return std::visit(overloaded {
        [](TextHashMethod & th) {
            return std::string{"text:"} + printHashType(htSHA256);
        },
        [](FixedOutputHashMethod & fshm) {
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
            [&](TextHashMethod & thm) {
                return ContentAddress(TextHash {
                    .hash = Hash::parseNonSRIUnprefixed(rest, htSHA256)
                });
            },
            [&](FixedOutputHashMethod & fohMethod) {
                return ContentAddress(FixedOutputHash {
                    .method = fohMethod.fileIngestionMethod,
                    .hash = Hash::parseNonSRIUnprefixed(rest, std::move(fohMethod.hashType)),
                });
            },
        }, caMethod);
}

ContentAddressMethod parseContentAddressMethod(std::string_view caMethod)
{
    std::string asPrefix = std::string{caMethod} + ":";
    // parseContentAddressMethodPrefix takes its argument by reference
    std::string_view asPrefixView = asPrefix;
    return parseContentAddressMethodPrefix(asPrefixView);
}

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt)
{
    return rawCaOpt == "" ? std::optional<ContentAddress>() : parseContentAddress(rawCaOpt);
};

std::string renderContentAddress(std::optional<ContentAddress> ca)
{
    return ca ? renderContentAddress(*ca) : "";
}

Hash getContentAddressHash(const ContentAddress & ca)
{
    return std::visit(overloaded {
        [](const TextHash & th) {
            return th.hash;
        },
        [](const FixedOutputHash & fsh) {
            return fsh.hash;
        },
    }, ca);
}

ContentAddressWithReferences caWithoutRefs(const ContentAddress & ca) {
    return std::visit(overloaded {
        [&](const TextHash & h) -> ContentAddressWithReferences {
            return TextInfo { h, {}};
        },
        [&](const FixedOutputHash & h) -> ContentAddressWithReferences {
            return FixedOutputInfo { h, {}};
        },
    }, ca);
}

}

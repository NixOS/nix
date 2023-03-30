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
    default:
        throw Error("impossible, caught both cases");
    }
}

std::string ContentAddress::render() const
{
    return std::visit(overloaded {
        [](const TextHash & th) {
            return "text:"
                + th.hash.to_string(Base32, true);
        },
        [](const FixedOutputHash & fsh) {
            return "fixed:"
                + makeFileIngestionPrefix(fsh.method)
                + fsh.hash.to_string(Base32, true);
        }
    }, raw);
}

std::string ContentAddressMethod::render() const
{
    return std::visit(overloaded {
        [](const TextHashMethod & th) {
            return std::string{"text:"} + printHashType(htSHA256);
        },
        [](const FixedOutputHashMethod & fshm) {
            return "fixed:" + makeFileIngestionPrefix(fshm.fileIngestionMethod) + printHashType(fshm.hashType);
        }
    }, raw);
}

/**
 * Parses content address strings up to the hash.
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

ContentAddress ContentAddress::parse(std::string_view rawCa) {
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
        }, caMethod.raw);
}

ContentAddressMethod ContentAddressMethod::parse(std::string_view caMethod)
{
    std::string asPrefix = std::string{caMethod} + ":";
    // parseContentAddressMethodPrefix takes its argument by reference
    std::string_view asPrefixView = asPrefix;
    return parseContentAddressMethodPrefix(asPrefixView);
}

std::optional<ContentAddress> ContentAddress::parseOpt(std::string_view rawCaOpt)
{
    return rawCaOpt == ""
        ? std::nullopt
        : std::optional { ContentAddress::parse(rawCaOpt) };
};

std::string renderContentAddress(std::optional<ContentAddress> ca)
{
    return ca ? ca->render() : "";
}

const Hash & ContentAddress::getHash() const
{
    return std::visit(overloaded {
        [](const TextHash & th) -> auto & {
            return th.hash;
        },
        [](const FixedOutputHash & fsh) -> auto & {
            return fsh.hash;
        },
    }, raw);
}

bool StoreReferences::empty() const
{
    return !self && others.empty();
}

size_t StoreReferences::size() const
{
    return (self ? 1 : 0) + others.size();
}

ContentAddressWithReferences ContentAddressWithReferences::withoutRefs(const ContentAddress & ca) {
    return std::visit(overloaded {
        [&](const TextHash & h) -> ContentAddressWithReferences {
            return TextInfo {
                .hash = h,
                .references = {},
            };
        },
        [&](const FixedOutputHash & h) -> ContentAddressWithReferences {
            return FixedOutputInfo {
                .hash = h,
                .references = {},
            };
        },
    }, ca.raw);
}

}

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

std::string ContentAddressMethod::renderPrefix() const {
    return std::visit(overloaded {
        [](TextIngestionMethod) -> std::string { return "text:"; },
        [](FileIngestionMethod m2) {
             /* Not prefixed for back compat with things that couldn't produce text before. */
            return makeFileIngestionPrefix(m2);
        },
    }, raw);
}

ContentAddressMethod ContentAddressMethod::parsePrefix(std::string_view & m)
{
    ContentAddressMethod method = FileIngestionMethod::Flat;
    if (splitPrefix(m, "r:"))
        method = FileIngestionMethod::Recursive;
    else if (splitPrefix(m, "text:"))
        method = TextIngestionMethod {};
    return method;
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

std::string ContentAddressMethod::render(HashType ht) const
{
    return std::visit(overloaded {
        [&](const TextIngestionMethod & th) {
            return std::string{"text:"} + printHashType(ht);
        },
        [&](const FileIngestionMethod & fim) {
            return "fixed:" + makeFileIngestionPrefix(fim) + printHashType(ht);
        }
    }, raw);
}

/**
 * Parses content address strings up to the hash.
 */
static std::pair<ContentAddressMethod, HashType> parseContentAddressMethodPrefix(std::string_view & rest)
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
        return {
            TextIngestionMethod {},
            std::move(hashType),
        };
    } else if (prefix == "fixed") {
        // Parse method
        auto method = FileIngestionMethod::Flat;
        if (splitPrefix(rest, "r:"))
            method = FileIngestionMethod::Recursive;
        HashType hashType = parseHashType_();
        return {
            std::move(method),
            std::move(hashType),
        };
    } else
        throw UsageError("content address prefix '%s' is unrecognized. Recogonized prefixes are 'text' or 'fixed'", prefix);
}

ContentAddress ContentAddress::parse(std::string_view rawCa) {
    auto rest = rawCa;

    auto [caMethod, hashType_] = parseContentAddressMethodPrefix(rest);
    auto hashType = hashType_; // work around clang bug

    return std::visit(overloaded {
        [&](TextIngestionMethod &) {
            return ContentAddress(TextHash {
                .hash = Hash::parseNonSRIUnprefixed(rest, hashType)
            });
        },
        [&](FileIngestionMethod & fim) {
            return ContentAddress(FixedOutputHash {
                .method = fim,
                .hash = Hash::parseNonSRIUnprefixed(rest, hashType),
            });
        },
    }, caMethod.raw);
}

std::pair<ContentAddressMethod, HashType> ContentAddressMethod::parse(std::string_view caMethod)
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

ContentAddress ContentAddress::fromParts(
    ContentAddressMethod method, Hash hash)
{
    return std::visit(overloaded {
        [&](TextIngestionMethod _) -> ContentAddress {
            return TextHash {
                .hash = std::move(hash),
            };
        },
        [&](FileIngestionMethod m2) -> ContentAddress {
            return FixedOutputHash {
                .method = std::move(m2),
                .hash = std::move(hash),
            };
        },
    }, method.raw);
}

ContentAddressMethod ContentAddress::getMethod() const
{
    return std::visit(overloaded {
        [](const TextHash & th) -> ContentAddressMethod {
            return TextIngestionMethod {};
        },
        [](const FixedOutputHash & fsh) -> ContentAddressMethod {
            return fsh.method;
        },
    }, raw);
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

ContentAddressWithReferences ContentAddressWithReferences::fromParts(
    ContentAddressMethod method, Hash hash, StoreReferences refs)
{
    return std::visit(overloaded {
        [&](TextIngestionMethod _) -> ContentAddressWithReferences {
            if (refs.self)
                throw UsageError("Cannot have a self reference with text hashing scheme");
            return TextInfo {
                .hash = { .hash = std::move(hash) },
                .references = std::move(refs.others),
            };
        },
        [&](FileIngestionMethod m2) -> ContentAddressWithReferences {
            return FixedOutputInfo {
                .hash = {
                    .method = m2,
                    .hash = std::move(hash),
                },
                .references = std::move(refs),
            };
        },
    }, method.raw);
}

ContentAddressMethod ContentAddressWithReferences::getMethod() const
{
    return std::visit(overloaded {
        [](const TextInfo & th) -> ContentAddressMethod {
            return TextIngestionMethod {};
        },
        [](const FixedOutputInfo & fsh) -> ContentAddressMethod {
            return fsh.hash.method;
        },
    }, raw);
}

Hash ContentAddressWithReferences::getHash() const
{
    return std::visit(overloaded {
        [](const TextInfo & th) {
            return th.hash.hash;
        },
        [](const FixedOutputInfo & fsh) {
            return fsh.hash.hash;
        },
    }, raw);
}

std::string ContentAddressWithReferences::printMethodAlgo() const {
    return getMethod().renderPrefix()
        + printHashType(getHash().type);
}

}

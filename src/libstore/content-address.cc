#include "args.hh"
#include "content-address.hh"
#include "split.hh"

namespace nix {

std::string makeFileIngestionPrefix(FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    case FileIngestionMethod::Git:
        return "git:";
    default:
        throw Error("impossible, caught both cases");
    }
}

std::string ContentAddressMethod::renderPrefix() const
{
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
    if (splitPrefix(m, "git:"))
        method = FileIngestionMethod::Git;
    else if (splitPrefix(m, "text:"))
        method = TextIngestionMethod {};
    return method;
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

std::string ContentAddress::render() const
{
    return std::visit(overloaded {
        [](const TextIngestionMethod &) -> std::string {
            return "text:";
        },
        [](const FileIngestionMethod & method) {
            return "fixed:"
                + makeFileIngestionPrefix(method);
        },
    }, method.raw)
        + this->hash.to_string(Base32, true);
}

static HashType parseHashType_(std::string_view & rest) {
    auto hashTypeRaw = splitPrefixTo(rest, ':');
    if (!hashTypeRaw)
        throw UsageError("hash must be in form \"<algo>:<hash>\", but found: %s", rest);
    return parseHashType(*hashTypeRaw);
};

static FileIngestionMethod parseFileIngestionMethod_(std::string_view & rest) {
    if (splitPrefix(rest, "r:"))
        return FileIngestionMethod::Recursive;
    else if (splitPrefix(rest, "git:"))
        return FileIngestionMethod::Git;
    return FileIngestionMethod::Flat;
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
            throw UsageError("not a path-info content address because it is not in the form \"<prefix>:<rest>\": %s", wholeInput);
        prefix = *optPrefix;
    }

    // Switch on prefix
    if (prefix == "text") {
        // No parsing of the ingestion method, "text" only support flat.
        HashType hashType = parseHashType_(rest);
        return {
            TextIngestionMethod {},
            std::move(hashType),
        };
    } else if (prefix == "fixed") {
        // Parse method
        auto method = FileIngestionMethod::Flat;
        if (splitPrefix(rest, "r:"))
            method = FileIngestionMethod::Recursive;
        if (splitPrefix(rest, "git:"))
            method = FileIngestionMethod::Git;
        HashType hashType = parseHashType_(rest);
        return {
            std::move(method),
            std::move(hashType),
        };
    } else
        throw UsageError("path-info content address prefix \"%s\" is unrecognized. Recogonized prefixes are \"text\" or \"fixed\"", prefix);
}

ContentAddress ContentAddress::parse(std::string_view rawCa)
{
    auto rest = rawCa;

    auto [caMethod, hashType] = parseContentAddressMethodPrefix(rest);

    return ContentAddress {
        .method = std::move(caMethod),
        .hash = Hash::parseNonSRIUnprefixed(rest, hashType),
    };
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


// FIXME Deduplicate with store-api.cc path computation
std::string renderStorePathDescriptor(StorePathDescriptor ca)
{
    std::string result { ca.name };
    result += ":";

    auto dumpRefs = [&](auto references, bool hasSelfReference) {
        result += "refs:";
        result += std::to_string(references.size());
        for (auto & i : references) {
            result += ":";
            result += i.to_string();
        }
        if (hasSelfReference) result += ":self";
        result += ":";
    };

    std::visit(overloaded {
        [&](const TextInfo & th) {
            result += "text:";
            dumpRefs(th.references, false);
            result += th.hash.to_string(Base32, true);
        },
        [&](const FixedOutputInfo & fsh) {
            result += "fixed:";
            dumpRefs(fsh.references.others, fsh.references.self);
            result += makeFileIngestionPrefix(fsh.method);
            result += fsh.hash.to_string(Base32, true);
        },
    }, ca.info.raw);

    return result;
}


StorePathDescriptor parseStorePathDescriptor(std::string_view rawCa)
{
    warn("%s", rawCa);
    auto rest = rawCa;

    std::string_view name;
    std::string_view tag;
    {
        auto optName = splitPrefixTo(rest, ':');
        auto optTag = splitPrefixTo(rest, ':');
        if (!(optTag && optName))
            throw UsageError("not a content address because it is not in the form \"<name>:<tag>:<rest>\": %s", rawCa);
        tag = *optTag;
        name = *optName;
    }

    auto parseRefs = [&]() -> StoreReferences {
        if (!splitPrefix(rest, "refs:"))
            throw Error("Invalid CA \"%s\", \"%s\" should begin with \"refs:\"", rawCa, rest);
        StoreReferences ret;
        size_t numReferences = 0;
        {
            auto countRaw = splitPrefixTo(rest, ':');
            if (!countRaw)
                throw UsageError("Invalid count");
            numReferences = std::stoi(std::string { *countRaw });
        }
        for (size_t i = 0; i < numReferences; i++) {
            auto s = splitPrefixTo(rest, ':');
            if (!s)
                throw UsageError("Missing reference no. %d", i);
            ret.others.insert(StorePath(*s));
        }
        if (splitPrefix(rest, "self:"))
            ret.self = true;
        return ret;
    };

    // Dummy value
    ContentAddressWithReferences info = TextInfo { Hash(htSHA256), {} };

    // Switch on tag
    if (tag == "text") {
        auto refs = parseRefs();
        if (refs.self)
            throw UsageError("Text content addresses cannot have self references");
        auto hashType = parseHashType_(rest);
        if (hashType != htSHA256)
            throw Error("Text content address hash should use %s, but instead uses %s",
                printHashType(htSHA256), printHashType(hashType));
        info = TextInfo {
            .hash = Hash::parseNonSRIUnprefixed(rest, std::move(hashType)),
            .references = refs.others,
        };
    } else if (tag == "fixed") {
        auto refs = parseRefs();
        auto method = parseFileIngestionMethod_(rest);
        auto hashType = parseHashType_(rest);
        info = FixedOutputInfo {
            .method = method,
            .hash = Hash::parseNonSRIUnprefixed(rest, std::move(hashType)),
            .references = refs,
        };
    } else
        throw UsageError("content address tag \"%s\" is unrecognized. Recogonized tages are \"text\" or \"fixed\"", tag);

    return StorePathDescriptor {
        .name = std::string { name },
        .info = info,
    };
}


std::string ContentAddress::printMethodAlgo() const
{
    return method.renderPrefix()
        + printHashType(hash.type);
}

ContentAddressWithReferences ContentAddressWithReferences::withoutRefs(const ContentAddress & ca) noexcept
{
    return std::visit(overloaded {
        [&](const TextIngestionMethod &) -> ContentAddressWithReferences {
            return TextInfo {
                .hash = ca.hash,
                .references = {},
            };
        },
        [&](const FileIngestionMethod & method) -> ContentAddressWithReferences {
            return FixedOutputInfo {
                .method = method,
                .hash = ca.hash,
                .references = {},
            };
        },
    }, ca.method.raw);
}

std::optional<ContentAddressWithReferences> ContentAddressWithReferences::fromPartsOpt(
    ContentAddressMethod method, Hash hash, StoreReferences refs) noexcept
{
    return std::visit(overloaded {
        [&](TextIngestionMethod _) -> std::optional<ContentAddressWithReferences> {
            if (refs.self)
                return std::nullopt;
            return ContentAddressWithReferences {
                TextInfo {
                    .hash = std::move(hash),
                    .references = std::move(refs.others),
                }
            };
        },
        [&](FileIngestionMethod m2) -> std::optional<ContentAddressWithReferences> {
            return ContentAddressWithReferences {
                FixedOutputInfo {
                    .method = m2,
                    .hash = std::move(hash),
                    .references = std::move(refs),
                }
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
            return fsh.method;
        },
    }, raw);
}

Hash ContentAddressWithReferences::getHash() const
{
    return std::visit(overloaded {
        [](const TextInfo & th) {
            return th.hash;
        },
        [](const FixedOutputInfo & fsh) {
            return fsh.hash;
        },
    }, raw);
}

}

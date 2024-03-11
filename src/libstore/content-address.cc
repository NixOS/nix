#include "args.hh"
#include "content-address.hh"
#include "split.hh"

namespace nix {

std::string_view makeFileIngestionPrefix(FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        return "";
    case FileIngestionMethod::Recursive:
        return "r:";
    case FileIngestionMethod::Git:
        experimentalFeatureSettings.require(Xp::GitHashing);
        return "git:";
    default:
        throw Error("impossible, caught both cases");
    }
}

std::string_view ContentAddressMethod::render() const
{
    return std::visit(overloaded {
        [](TextIngestionMethod) -> std::string_view { return "text"; },
        [](FileIngestionMethod m2) {
             /* Not prefixed for back compat with things that couldn't produce text before. */
            return renderFileIngestionMethod(m2);
        },
    }, raw);
}

ContentAddressMethod ContentAddressMethod::parse(std::string_view m)
{
    if (m == "text")
        return TextIngestionMethod {};
    else
        return parseFileIngestionMethod(m);
}

std::string_view ContentAddressMethod::renderPrefix() const
{
    return std::visit(overloaded {
        [](TextIngestionMethod) -> std::string_view { return "text:"; },
        [](FileIngestionMethod m2) {
             /* Not prefixed for back compat with things that couldn't produce text before. */
            return makeFileIngestionPrefix(m2);
        },
    }, raw);
}

ContentAddressMethod ContentAddressMethod::parsePrefix(std::string_view & m)
{
    if (splitPrefix(m, "r:")) {
        return FileIngestionMethod::Recursive;
    }
    else if (splitPrefix(m, "git:")) {
        experimentalFeatureSettings.require(Xp::GitHashing);
        return FileIngestionMethod::Git;
    }
    else if (splitPrefix(m, "text:")) {
        return TextIngestionMethod {};
    }
    return FileIngestionMethod::Flat;
}

std::string ContentAddressMethod::renderWithAlgo(HashAlgorithm ha) const
{
    return std::visit(overloaded {
        [&](const TextIngestionMethod & th) {
            return std::string{"text:"} + printHashAlgo(ha);
        },
        [&](const FileIngestionMethod & fim) {
            return "fixed:" + makeFileIngestionPrefix(fim) + printHashAlgo(ha);
        }
    }, raw);
}

FileIngestionMethod ContentAddressMethod::getFileIngestionMethod() const
{
    return std::visit(overloaded {
        [&](const TextIngestionMethod & th) {
            return FileIngestionMethod::Flat;
        },
        [&](const FileIngestionMethod & fim) {
            return fim;
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
        + this->hash.to_string(HashFormat::Nix32, true);
}

/**
 * Parses content address strings up to the hash.
 */
static std::pair<ContentAddressMethod, HashAlgorithm> parseContentAddressMethodPrefix(std::string_view & rest)
{
    std::string_view wholeInput { rest };

    std::string_view prefix;
    {
        auto optPrefix = splitPrefixTo(rest, ':');
        if (!optPrefix)
            throw UsageError("not a content address because it is not in the form '<prefix>:<rest>': %s", wholeInput);
        prefix = *optPrefix;
    }

    auto parseHashAlgorithm_ = [&](){
        auto hashAlgoRaw = splitPrefixTo(rest, ':');
        if (!hashAlgoRaw)
            throw UsageError("content address hash must be in form '<algo>:<hash>', but found: %s", wholeInput);
        HashAlgorithm hashAlgo = parseHashAlgo(*hashAlgoRaw);
        return hashAlgo;
    };

    // Switch on prefix
    if (prefix == "text") {
        // No parsing of the ingestion method, "text" only support flat.
        HashAlgorithm hashAlgo = parseHashAlgorithm_();
        return {
            TextIngestionMethod {},
            std::move(hashAlgo),
        };
    } else if (prefix == "fixed") {
        // Parse method
        auto method = FileIngestionMethod::Flat;
        if (splitPrefix(rest, "r:"))
            method = FileIngestionMethod::Recursive;
        else if (splitPrefix(rest, "git:")) {
            experimentalFeatureSettings.require(Xp::GitHashing);
            method = FileIngestionMethod::Git;
        }
        HashAlgorithm hashAlgo = parseHashAlgorithm_();
        return {
            std::move(method),
            std::move(hashAlgo),
        };
    } else
        throw UsageError("content address prefix '%s' is unrecognized. Recogonized prefixes are 'text' or 'fixed'", prefix);
}

ContentAddress ContentAddress::parse(std::string_view rawCa)
{
    auto rest = rawCa;

    auto [caMethod, hashAlgo] = parseContentAddressMethodPrefix(rest);

    return ContentAddress {
        .method = std::move(caMethod),
        .hash = Hash::parseNonSRIUnprefixed(rest, hashAlgo),
    };
}

std::pair<ContentAddressMethod, HashAlgorithm> ContentAddressMethod::parseWithAlgo(std::string_view caMethod)
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

std::string ContentAddress::printMethodAlgo() const
{
    return std::string { method.renderPrefix() }
        + printHashAlgo(hash.algo);
}

bool StoreReferences::empty() const
{
    return !self && others.empty();
}

size_t StoreReferences::size() const
{
    return (self ? 1 : 0) + others.size();
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

ContentAddressWithReferences ContentAddressWithReferences::fromParts(
    ContentAddressMethod method, Hash hash, StoreReferences refs)
{
    return std::visit(overloaded {
        [&](TextIngestionMethod _) -> ContentAddressWithReferences {
            if (refs.self)
                throw Error("self-reference not allowed with text hashing");
            return ContentAddressWithReferences {
                TextInfo {
                    .hash = std::move(hash),
                    .references = std::move(refs.others),
                }
            };
        },
        [&](FileIngestionMethod m2) -> ContentAddressWithReferences {
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

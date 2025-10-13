#include "nix/util/args.hh"
#include "nix/store/content-address.hh"
#include "nix/util/split.hh"
#include "nix/util/json-utils.hh"

namespace nix {

std::string_view makeFileIngestionPrefix(FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        // Not prefixed for back compat
        return "";
    case FileIngestionMethod::NixArchive:
        return "r:";
    case FileIngestionMethod::Git:
        experimentalFeatureSettings.require(Xp::GitHashing);
        return "git:";
    default:
        assert(false);
    }
}

std::string_view ContentAddressMethod::render() const
{
    switch (raw) {
    case ContentAddressMethod::Raw::Text:
        return "text";
    case ContentAddressMethod::Raw::Flat:
    case ContentAddressMethod::Raw::NixArchive:
    case ContentAddressMethod::Raw::Git:
        return renderFileIngestionMethod(getFileIngestionMethod());
    default:
        assert(false);
    }
}

/**
 * **Not surjective**
 *
 * This is not exposed because `FileIngestionMethod::Flat` maps to
 * `ContentAddressMethod::Raw::Flat` and
 * `ContentAddressMethod::Raw::Text` alike. We can thus only safely use
 * this when the latter is ruled out (e.g. because it is already
 * handled).
 */
static ContentAddressMethod fileIngestionMethodToContentAddressMethod(FileIngestionMethod m)
{
    switch (m) {
    case FileIngestionMethod::Flat:
        return ContentAddressMethod::Raw::Flat;
    case FileIngestionMethod::NixArchive:
        return ContentAddressMethod::Raw::NixArchive;
    case FileIngestionMethod::Git:
        return ContentAddressMethod::Raw::Git;
    default:
        assert(false);
    }
}

ContentAddressMethod ContentAddressMethod::parse(std::string_view m)
{
    if (m == "text")
        return ContentAddressMethod::Raw::Text;
    else
        return fileIngestionMethodToContentAddressMethod(parseFileIngestionMethod(m));
}

std::string_view ContentAddressMethod::renderPrefix() const
{
    switch (raw) {
    case ContentAddressMethod::Raw::Text:
        return "text:";
    case ContentAddressMethod::Raw::Flat:
    case ContentAddressMethod::Raw::NixArchive:
    case ContentAddressMethod::Raw::Git:
        return makeFileIngestionPrefix(getFileIngestionMethod());
    default:
        assert(false);
    }
}

ContentAddressMethod ContentAddressMethod::parsePrefix(std::string_view & m)
{
    if (splitPrefix(m, "r:")) {
        return ContentAddressMethod::Raw::NixArchive;
    } else if (splitPrefix(m, "git:")) {
        experimentalFeatureSettings.require(Xp::GitHashing);
        return ContentAddressMethod::Raw::Git;
    } else if (splitPrefix(m, "text:")) {
        return ContentAddressMethod::Raw::Text;
    }
    return ContentAddressMethod::Raw::Flat;
}

/**
 * This is slightly more mindful of forward compat in that it uses `fixed:`
 * rather than just doing a raw empty prefix or `r:`, which doesn't "save room"
 * for future changes very well.
 */
static std::string renderPrefixModern(const ContentAddressMethod & ca)
{
    switch (ca.raw) {
    case ContentAddressMethod::Raw::Text:
        return "text:";
    case ContentAddressMethod::Raw::Flat:
    case ContentAddressMethod::Raw::NixArchive:
    case ContentAddressMethod::Raw::Git:
        return "fixed:" + makeFileIngestionPrefix(ca.getFileIngestionMethod());
    default:
        assert(false);
    }
}

std::string ContentAddressMethod::renderWithAlgo(HashAlgorithm ha) const
{
    return renderPrefixModern(*this) + printHashAlgo(ha);
}

FileIngestionMethod ContentAddressMethod::getFileIngestionMethod() const
{
    switch (raw) {
    case ContentAddressMethod::Raw::Flat:
        return FileIngestionMethod::Flat;
    case ContentAddressMethod::Raw::NixArchive:
        return FileIngestionMethod::NixArchive;
    case ContentAddressMethod::Raw::Git:
        return FileIngestionMethod::Git;
    case ContentAddressMethod::Raw::Text:
        return FileIngestionMethod::Flat;
    default:
        assert(false);
    }
}

std::string ContentAddress::render() const
{
    return renderPrefixModern(method) + this->hash.to_string(HashFormat::Nix32, true);
}

/**
 * Parses content address strings up to the hash.
 */
static std::pair<ContentAddressMethod, HashAlgorithm> parseContentAddressMethodPrefix(std::string_view & rest)
{
    std::string_view wholeInput{rest};

    std::string_view prefix;
    {
        auto optPrefix = splitPrefixTo(rest, ':');
        if (!optPrefix)
            throw UsageError("not a content address because it is not in the form '<prefix>:<rest>': %s", wholeInput);
        prefix = *optPrefix;
    }

    auto parseHashAlgorithm_ = [&]() {
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
            ContentAddressMethod::Raw::Text,
            std::move(hashAlgo),
        };
    } else if (prefix == "fixed") {
        // Parse method
        auto method = ContentAddressMethod::Raw::Flat;
        if (splitPrefix(rest, "r:"))
            method = ContentAddressMethod::Raw::NixArchive;
        else if (splitPrefix(rest, "git:")) {
            experimentalFeatureSettings.require(Xp::GitHashing);
            method = ContentAddressMethod::Raw::Git;
        }
        HashAlgorithm hashAlgo = parseHashAlgorithm_();
        return {
            std::move(method),
            std::move(hashAlgo),
        };
    } else
        throw UsageError(
            "content address prefix '%s' is unrecognized. Recogonized prefixes are 'text' or 'fixed'", prefix);
}

ContentAddress ContentAddress::parse(std::string_view rawCa)
{
    auto rest = rawCa;

    auto [caMethod, hashAlgo] = parseContentAddressMethodPrefix(rest);

    return ContentAddress{
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
    return rawCaOpt == "" ? std::nullopt : std::optional{ContentAddress::parse(rawCaOpt)};
};

std::string renderContentAddress(std::optional<ContentAddress> ca)
{
    return ca ? ca->render() : "";
}

std::string ContentAddress::printMethodAlgo() const
{
    return std::string{method.renderPrefix()} + printHashAlgo(hash.algo);
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
    switch (ca.method.raw) {
    case ContentAddressMethod::Raw::Text:
        return TextInfo{
            .hash = ca.hash,
            .references = {},
        };
    case ContentAddressMethod::Raw::Flat:
    case ContentAddressMethod::Raw::NixArchive:
    case ContentAddressMethod::Raw::Git:
        return FixedOutputInfo{
            .method = ca.method.getFileIngestionMethod(),
            .hash = ca.hash,
            .references = {},
        };
    default:
        assert(false);
    }
}

ContentAddressWithReferences
ContentAddressWithReferences::fromParts(ContentAddressMethod method, Hash hash, StoreReferences refs)
{
    switch (method.raw) {
    case ContentAddressMethod::Raw::Text:
        if (refs.self)
            throw Error("self-reference not allowed with text hashing");
        return TextInfo{
            .hash = std::move(hash),
            .references = std::move(refs.others),
        };
    case ContentAddressMethod::Raw::Flat:
    case ContentAddressMethod::Raw::NixArchive:
    case ContentAddressMethod::Raw::Git:
        return FixedOutputInfo{
            .method = method.getFileIngestionMethod(),
            .hash = std::move(hash),
            .references = std::move(refs),
        };
    default:
        assert(false);
    }
}

ContentAddressMethod ContentAddressWithReferences::getMethod() const
{
    return std::visit(
        overloaded{
            [](const TextInfo & th) -> ContentAddressMethod { return ContentAddressMethod::Raw::Text; },
            [](const FixedOutputInfo & fsh) -> ContentAddressMethod {
                return fileIngestionMethodToContentAddressMethod(fsh.method);
            },
        },
        raw);
}

Hash ContentAddressWithReferences::getHash() const
{
    return std::visit(
        overloaded{
            [](const TextInfo & th) { return th.hash; },
            [](const FixedOutputInfo & fsh) { return fsh.hash; },
        },
        raw);
}

} // namespace nix

namespace nlohmann {

using namespace nix;

ContentAddressMethod adl_serializer<ContentAddressMethod>::from_json(const json & json)
{
    return ContentAddressMethod::parse(getString(json));
}

void adl_serializer<ContentAddressMethod>::to_json(json & json, const ContentAddressMethod & m)
{
    json = m.render();
}

ContentAddress adl_serializer<ContentAddress>::from_json(const json & json)
{
    auto obj = getObject(json);
    return {
        .method = adl_serializer<ContentAddressMethod>::from_json(valueAt(obj, "method")),
        .hash = valueAt(obj, "hash"),
    };
}

void adl_serializer<ContentAddress>::to_json(json & json, const ContentAddress & ca)
{
    json = {
        {"method", ca.method},
        {"hash", ca.hash},
    };
}

} // namespace nlohmann

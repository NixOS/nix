#include "fetchers.hh"
#include "store-api.hh"
#include "input-accessor.hh"
#include "source-path.hh"
#include "fetch-to-store.hh"
#include "json-utils.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

using InputSchemeMap = std::map<std::string_view, std::shared_ptr<InputScheme>>;

std::unique_ptr<InputSchemeMap> inputSchemes = nullptr;

void registerInputScheme(std::shared_ptr<InputScheme> && inputScheme)
{
    if (!inputSchemes)
        inputSchemes = std::make_unique<InputSchemeMap>();
    auto schemeName = inputScheme->schemeName();
    if (inputSchemes->count(schemeName) > 0)
        throw Error("Input scheme with name %s already registered", schemeName);
    inputSchemes->insert_or_assign(schemeName, std::move(inputScheme));
}

nlohmann::json dumpRegisterInputSchemeInfo() {
    using nlohmann::json;

    auto res = json::object();

    for (auto & [name, scheme] : *inputSchemes) {
        auto & r = res[name] = json::object();
        r["allowedAttrs"] = scheme->allowedAttrs();
    }

    return res;
}

Input Input::fromURL(const std::string & url, bool requireTree)
{
    return fromURL(parseURL(url), requireTree);
}

static void fixupInput(Input & input)
{
    // Check common attributes.
    input.getType();
    input.getRef();
    input.getRevCount();
    input.getLastModified();
}

Input Input::fromURL(const ParsedURL & url, bool requireTree)
{
    for (auto & [_, inputScheme] : *inputSchemes) {
        auto res = inputScheme->inputFromURL(url, requireTree);
        if (res) {
            experimentalFeatureSettings.require(inputScheme->experimentalFeature());
            res->scheme = inputScheme;
            fixupInput(*res);
            return std::move(*res);
        }
    }

    throw Error("input '%s' is unsupported", url.url);
}

Input Input::fromAttrs(Attrs && attrs)
{
    auto schemeName = ({
        auto schemeNameOpt = maybeGetStrAttr(attrs, "type");
        if (!schemeNameOpt)
            throw Error("'type' attribute to specify input scheme is required but not provided");
        *std::move(schemeNameOpt);
    });

    auto raw = [&]() {
        // Return an input without a scheme; most operations will fail,
        // but not all of them. Doing this is to support those other
        // operations which are supposed to be robust on
        // unknown/uninterpretable inputs.
        Input input;
        input.attrs = attrs;
        fixupInput(input);
        return input;
    };

    std::shared_ptr<InputScheme> inputScheme = ({
        auto i = inputSchemes->find(schemeName);
        i == inputSchemes->end() ? nullptr : i->second;
    });

    if (!inputScheme) return raw();

    experimentalFeatureSettings.require(inputScheme->experimentalFeature());

    auto allowedAttrs = inputScheme->allowedAttrs();

    for (auto & [name, _] : attrs)
        if (name != "type" && allowedAttrs.count(name) == 0)
            throw Error("input attribute '%s' not supported by scheme '%s'", name, schemeName);

    auto res = inputScheme->inputFromAttrs(attrs);
    if (!res) return raw();
    res->scheme = inputScheme;
    fixupInput(*res);
    return std::move(*res);
}

std::optional<std::string> Input::getFingerprint(ref<Store> store) const
{
    return scheme ? scheme->getFingerprint(store, *this) : std::nullopt;
}

ParsedURL Input::toURL() const
{
    if (!scheme)
        throw Error("cannot show unsupported input '%s'", attrsToJSON(attrs));
    return scheme->toURL(*this);
}

std::string Input::toURLString(const std::map<std::string, std::string> & extraQuery) const
{
    auto url = toURL();
    for (auto & attr : extraQuery)
        url.query.insert(attr);
    return url.to_string();
}

std::string Input::to_string() const
{
    return toURL().to_string();
}

bool Input::isDirect() const
{
    return !scheme || scheme->isDirect(*this);
}

bool Input::isLocked() const
{
    return scheme && scheme->isLocked(*this);
}

Attrs Input::toAttrs() const
{
    return attrs;
}

bool Input::operator ==(const Input & other) const
{
    return attrs == other.attrs;
}

bool Input::contains(const Input & other) const
{
    if (*this == other) return true;
    auto other2(other);
    other2.attrs.erase("ref");
    other2.attrs.erase("rev");
    if (*this == other2) return true;
    return false;
}

std::pair<StorePath, Input> Input::fetchToStore(ref<Store> store) const
{
    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    /* The tree may already be in the Nix store, or it could be
       substituted (which is often faster than fetching from the
       original source). So check that. */
    if (getNarHash()) {
        try {
            auto storePath = computeStorePath(*store);

            store->ensurePath(storePath);

            debug("using substituted/cached input '%s' in '%s'",
                to_string(), store->printStorePath(storePath));

            return {std::move(storePath), *this};
        } catch (Error & e) {
            debug("substitution of input '%s' failed: %s", to_string(), e.what());
        }
    }

    auto [storePath, input] = [&]() -> std::pair<StorePath, Input> {
        try {
            auto [accessor, final] = getAccessorUnchecked(store);

            auto storePath = nix::fetchToStore(*store, SourcePath(accessor), FetchMode::Copy, final.getName());

            auto narHash = store->queryPathInfo(storePath)->narHash;
            final.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

            scheme->checkLocks(*this, final);

            return {storePath, final};
        } catch (Error & e) {
            e.addTrace({}, "while fetching the input '%s'", to_string());
            throw;
        }
    }();

    return {std::move(storePath), input};
}

void InputScheme::checkLocks(const Input & specified, const Input & final) const
{
    if (auto prevNarHash = specified.getNarHash()) {
        if (final.getNarHash() != prevNarHash) {
            if (final.getNarHash())
                throw Error((unsigned int) 102, "NAR hash mismatch in input '%s', expected '%s' but got '%s'",
                    specified.to_string(), prevNarHash->to_string(HashFormat::SRI, true), final.getNarHash()->to_string(HashFormat::SRI, true));
            else
                throw Error((unsigned int) 102, "NAR hash mismatch in input '%s', expected '%s' but got none",
                    specified.to_string(), prevNarHash->to_string(HashFormat::SRI, true));
        }
    }

    if (auto prevLastModified = specified.getLastModified()) {
        if (final.getLastModified() != prevLastModified)
            throw Error("'lastModified' attribute mismatch in input '%s', expected %d",
                final.to_string(), *prevLastModified);
    }

    if (auto prevRev = specified.getRev()) {
        if (final.getRev() != prevRev)
            throw Error("'rev' attribute mismatch in input '%s', expected %s",
                final.to_string(), prevRev->gitRev());
    }

    if (auto prevRevCount = specified.getRevCount()) {
        if (final.getRevCount() != prevRevCount)
            throw Error("'revCount' attribute mismatch in input '%s', expected %d",
                final.to_string(), *prevRevCount);
    }
}

std::pair<ref<InputAccessor>, Input> Input::getAccessor(ref<Store> store) const
{
    try {
        auto [accessor, final] = getAccessorUnchecked(store);

        scheme->checkLocks(*this, final);

        return {accessor, std::move(final)};
    } catch (Error & e) {
        e.addTrace({}, "while fetching the input '%s'", to_string());
        throw;
    }
}

std::pair<ref<InputAccessor>, Input> Input::getAccessorUnchecked(ref<Store> store) const
{
    // FIXME: cache the accessor

    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    auto [accessor, final] = scheme->getAccessor(store, *this);

    accessor->fingerprint = scheme->getFingerprint(store, final);

    return {accessor, std::move(final)};
}

Input Input::applyOverrides(
    std::optional<std::string> ref,
    std::optional<Hash> rev) const
{
    if (!scheme) return *this;
    return scheme->applyOverrides(*this, ref, rev);
}

void Input::clone(const Path & destDir) const
{
    assert(scheme);
    scheme->clone(*this, destDir);
}

std::optional<Path> Input::getSourcePath() const
{
    assert(scheme);
    return scheme->getSourcePath(*this);
}

void Input::putFile(
    const CanonPath & path,
    std::string_view contents,
    std::optional<std::string> commitMsg) const
{
    assert(scheme);
    return scheme->putFile(*this, path, contents, commitMsg);
}

std::string Input::getName() const
{
    return maybeGetStrAttr(attrs, "name").value_or("source");
}

StorePath Input::computeStorePath(Store & store) const
{
    auto narHash = getNarHash();
    if (!narHash)
        throw Error("cannot compute store path for unlocked input '%s'", to_string());
    return store.makeFixedOutputPath(getName(), FixedOutputInfo {
        .method = FileIngestionMethod::Recursive,
        .hash = *narHash,
        .references = {},
    });
}

std::string Input::getType() const
{
    return getStrAttr(attrs, "type");
}

std::optional<Hash> Input::getNarHash() const
{
    if (auto s = maybeGetStrAttr(attrs, "narHash")) {
        auto hash = s->empty() ? Hash(HashAlgorithm::SHA256) : Hash::parseSRI(*s);
        if (hash.algo != HashAlgorithm::SHA256)
            throw UsageError("narHash must use SHA-256");
        return hash;
    }
    return {};
}

std::optional<std::string> Input::getRef() const
{
    if (auto s = maybeGetStrAttr(attrs, "ref"))
        return *s;
    return {};
}

std::optional<Hash> Input::getRev() const
{
    std::optional<Hash> hash = {};

    if (auto s = maybeGetStrAttr(attrs, "rev")) {
        try {
            hash = Hash::parseAnyPrefixed(*s);
        } catch (BadHash &e) {
            // Default to sha1 for backwards compatibility with existing
            // usages (e.g. `builtins.fetchTree` calls or flake inputs).
            hash = Hash::parseAny(*s, HashAlgorithm::SHA1);
        }
    }

    return hash;
}

std::optional<uint64_t> Input::getRevCount() const
{
    if (auto n = maybeGetIntAttr(attrs, "revCount"))
        return *n;
    return {};
}

std::optional<time_t> Input::getLastModified() const
{
    if (auto n = maybeGetIntAttr(attrs, "lastModified"))
        return *n;
    return {};
}

ParsedURL InputScheme::toURL(const Input & input) const
{
    throw Error("don't know how to convert input '%s' to a URL", attrsToJSON(input.attrs));
}

Input InputScheme::applyOverrides(
    const Input & input,
    std::optional<std::string> ref,
    std::optional<Hash> rev) const
{
    if (ref)
        throw Error("don't know how to set branch/tag name of input '%s' to '%s'", input.to_string(), *ref);
    if (rev)
        throw Error("don't know how to set revision of input '%s' to '%s'", input.to_string(), rev->gitRev());
    return input;
}

std::optional<Path> InputScheme::getSourcePath(const Input & input) const
{
    return {};
}

void InputScheme::putFile(
    const Input & input,
    const CanonPath & path,
    std::string_view contents,
    std::optional<std::string> commitMsg) const
{
    throw Error("input '%s' does not support modifying file '%s'", input.to_string(), path);
}

void InputScheme::clone(const Input & input, const Path & destDir) const
{
    throw Error("do not know how to clone input '%s'", input.to_string());
}

std::optional<ExperimentalFeature> InputScheme::experimentalFeature() const
{
    return {};
}

std::string publicKeys_to_string(const std::vector<PublicKey>& publicKeys)
{
    return ((nlohmann::json) publicKeys).dump();
}

}

namespace nlohmann {

using namespace nix;

fetchers::PublicKey adl_serializer<fetchers::PublicKey>::from_json(const json & json) {
    auto type = optionalValueAt(json, "type").value_or("ssh-ed25519");
    auto key = valueAt(json, "key");
    return fetchers::PublicKey { getString(type), getString(key) };
}

void adl_serializer<fetchers::PublicKey>::to_json(json & json, fetchers::PublicKey p) {
    json["type"] = p.type;
    json["key"] = p.key;
}

}

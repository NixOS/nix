#include "nix/fetchers/fetchers.hh"
#include "nix/store/store-api.hh"
#include "nix/util/source-path.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/util/json-utils.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/util/url.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

using InputSchemeMap = std::map<std::string_view, std::shared_ptr<InputScheme>>;

static InputSchemeMap & inputSchemes()
{
    static InputSchemeMap inputSchemeMap;
    return inputSchemeMap;
}

void registerInputScheme(std::shared_ptr<InputScheme> && inputScheme)
{
    auto schemeName = inputScheme->schemeName();
    if (!inputSchemes().emplace(schemeName, std::move(inputScheme)).second)
        throw Error("Input scheme with name %s already registered", schemeName);
}

nlohmann::json dumpRegisterInputSchemeInfo()
{
    using nlohmann::json;

    auto res = json::object();

    for (auto & [name, scheme] : inputSchemes()) {
        auto & r = res[name] = json::object();
        r["allowedAttrs"] = scheme->allowedAttrs();
    }

    return res;
}

Input Input::fromURL(const Settings & settings, const std::string & url, bool requireTree)
{
    return fromURL(settings, parseURL(url), requireTree);
}

static void fixupInput(Input & input)
{
    // Check common attributes.
    input.getType();
    input.getRef();
    input.getRevCount();
    input.getLastModified();
}

Input Input::fromURL(const Settings & settings, const ParsedURL & url, bool requireTree)
{
    for (auto & [_, inputScheme] : inputSchemes()) {
        auto res = inputScheme->inputFromURL(settings, url, requireTree);
        if (res) {
            experimentalFeatureSettings.require(inputScheme->experimentalFeature());
            res->scheme = inputScheme;
            fixupInput(*res);
            return std::move(*res);
        }
    }

    // Provide a helpful hint when user tries file+git instead of git+file
    auto parsedScheme = parseUrlScheme(url.scheme);
    if (parsedScheme.application == "file" && parsedScheme.transport == "git") {
        throw Error("input '%s' is unsupported; did you mean 'git+file' instead of 'file+git'?", url);
    }

    throw Error("input '%s' is unsupported", url);
}

Input Input::fromAttrs(const Settings & settings, Attrs && attrs)
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
        Input input{settings};
        input.attrs = attrs;
        fixupInput(input);
        return input;
    };

    std::shared_ptr<InputScheme> inputScheme = ({
        auto i = get(inputSchemes(), schemeName);
        i ? *i : nullptr;
    });

    if (!inputScheme)
        return raw();

    experimentalFeatureSettings.require(inputScheme->experimentalFeature());

    auto allowedAttrs = inputScheme->allowedAttrs();

    for (auto & [name, _] : attrs)
        if (name != "type" && name != "__final" && allowedAttrs.count(name) == 0)
            throw Error("input attribute '%s' not supported by scheme '%s'", name, schemeName);

    auto res = inputScheme->inputFromAttrs(settings, attrs);
    if (!res)
        return raw();
    res->scheme = inputScheme;
    fixupInput(*res);
    return std::move(*res);
}

std::optional<std::string> Input::getFingerprint(ref<Store> store) const
{
    if (!scheme)
        return std::nullopt;

    if (cachedFingerprint)
        return *cachedFingerprint;

    auto fingerprint = scheme->getFingerprint(store, *this);

    cachedFingerprint = fingerprint;

    return fingerprint;
}

ParsedURL Input::toURL() const
{
    if (!scheme)
        throw Error("cannot show unsupported input '%s'", attrsToJSON(attrs));
    return scheme->toURL(*this);
}

std::string Input::toURLString(const StringMap & extraQuery) const
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

bool Input::isFinal() const
{
    return maybeGetBoolAttr(attrs, "__final").value_or(false);
}

std::optional<std::string> Input::isRelative() const
{
    assert(scheme);
    return scheme->isRelative(*this);
}

Attrs Input::toAttrs() const
{
    return attrs;
}

bool Input::operator==(const Input & other) const noexcept
{
    return attrs == other.attrs;
}

bool Input::contains(const Input & other) const
{
    if (*this == other)
        return true;
    auto other2(other);
    other2.attrs.erase("ref");
    other2.attrs.erase("rev");
    if (*this == other2)
        return true;
    return false;
}

// FIXME: remove
std::pair<StorePath, Input> Input::fetchToStore(ref<Store> store) const
{
    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    auto [storePath, input] = [&]() -> std::pair<StorePath, Input> {
        try {
            auto [accessor, result] = getAccessorUnchecked(store);

            auto storePath =
                nix::fetchToStore(*settings, *store, SourcePath(accessor), FetchMode::Copy, result.getName());

            auto narHash = store->queryPathInfo(storePath)->narHash;
            result.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

            result.attrs.insert_or_assign("__final", Explicit<bool>(true));

            assert(result.isFinal());

            checkLocks(*this, result);

            return {storePath, result};
        } catch (Error & e) {
            e.addTrace({}, "while fetching the input '%s'", to_string());
            throw;
        }
    }();

    return {std::move(storePath), input};
}

void Input::checkLocks(Input specified, Input & result)
{
    /* If the original input is final, then we just return the
       original attributes, dropping any new fields returned by the
       fetcher. However, any fields that are in both the specified and
       result input must be identical. */
    if (specified.isFinal()) {

        /* Backwards compatibility hack: we had some lock files in the
           past that 'narHash' fields with incorrect base-64
           formatting (lacking the trailing '=', e.g. 'sha256-ri...Mw'
           instead of ''sha256-ri...Mw='). So fix that. */
        if (auto prevNarHash = specified.getNarHash())
            specified.attrs.insert_or_assign("narHash", prevNarHash->to_string(HashFormat::SRI, true));

        for (auto & field : specified.attrs) {
            auto field2 = result.attrs.find(field.first);
            if (field2 != result.attrs.end() && field.second != field2->second)
                throw Error(
                    "mismatch in field '%s' of input '%s', got '%s'",
                    field.first,
                    attrsToJSON(specified.attrs),
                    attrsToJSON(result.attrs));
        }

        result.attrs = specified.attrs;

        return;
    }

    if (auto prevNarHash = specified.getNarHash()) {
        if (result.getNarHash() != prevNarHash) {
            if (result.getNarHash())
                throw Error(
                    (unsigned int) 102,
                    "NAR hash mismatch in input '%s', expected '%s' but got '%s'",
                    specified.to_string(),
                    prevNarHash->to_string(HashFormat::SRI, true),
                    result.getNarHash()->to_string(HashFormat::SRI, true));
            else
                throw Error(
                    (unsigned int) 102,
                    "NAR hash mismatch in input '%s', expected '%s' but got none",
                    specified.to_string(),
                    prevNarHash->to_string(HashFormat::SRI, true));
        }
    }

    if (auto prevLastModified = specified.getLastModified()) {
        if (result.getLastModified() != prevLastModified)
            throw Error(
                "'lastModified' attribute mismatch in input '%s', expected %d, got %d",
                result.to_string(),
                *prevLastModified,
                result.getLastModified().value_or(-1));
    }

    if (auto prevRev = specified.getRev()) {
        if (result.getRev() != prevRev)
            throw Error("'rev' attribute mismatch in input '%s', expected %s", result.to_string(), prevRev->gitRev());
    }

    if (auto prevRevCount = specified.getRevCount()) {
        if (result.getRevCount() != prevRevCount)
            throw Error("'revCount' attribute mismatch in input '%s', expected %d", result.to_string(), *prevRevCount);
    }
}

std::pair<ref<SourceAccessor>, Input> Input::getAccessor(ref<Store> store) const
{
    try {
        auto [accessor, result] = getAccessorUnchecked(store);

        result.attrs.insert_or_assign("__final", Explicit<bool>(true));

        checkLocks(*this, result);

        return {accessor, std::move(result)};
    } catch (Error & e) {
        e.addTrace({}, "while fetching the input '%s'", to_string());
        throw;
    }
}

std::pair<ref<SourceAccessor>, Input> Input::getAccessorUnchecked(ref<Store> store) const
{
    // FIXME: cache the accessor

    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    /* The tree may already be in the Nix store, or it could be
       substituted (which is often faster than fetching from the
       original source). So check that. We only do this for final
       inputs, otherwise there is a risk that we don't return the
       same attributes (like `lastModified`) that the "real" fetcher
       would return.

       FIXME: add a setting to disable this.
       FIXME: substituting may be slower than fetching normally,
       e.g. for fetchers like Git that are incremental!
    */
    if (isFinal() && getNarHash()) {
        try {
            auto storePath = computeStorePath(*store);

            store->ensurePath(storePath);

            debug("using substituted/cached input '%s' in '%s'", to_string(), store->printStorePath(storePath));

            auto accessor = store->requireStoreObjectAccessor(storePath);

            accessor->fingerprint = getFingerprint(store);

            // Store a cache entry for the substituted tree so later fetches
            // can reuse the existing nar instead of copying the unpacked
            // input back into the store on every evaluation.
            if (accessor->fingerprint) {
                ContentAddressMethod method = ContentAddressMethod::Raw::NixArchive;
                auto cacheKey = makeFetchToStoreCacheKey(getName(), *accessor->fingerprint, method, "/");
                settings->getCache()->upsert(cacheKey, *store, {}, storePath);
            }

            accessor->setPathDisplay("«" + to_string() + "»");

            return {accessor, *this};
        } catch (Error & e) {
            debug("substitution of input '%s' failed: %s", to_string(), e.what());
        }
    }

    auto [accessor, result] = scheme->getAccessor(store, *this);

    if (!accessor->fingerprint)
        accessor->fingerprint = result.getFingerprint(store);
    else
        result.cachedFingerprint = accessor->fingerprint;

    return {accessor, std::move(result)};
}

Input Input::applyOverrides(std::optional<std::string> ref, std::optional<Hash> rev) const
{
    if (!scheme)
        return *this;
    return scheme->applyOverrides(*this, ref, rev);
}

void Input::clone(const Path & destDir) const
{
    assert(scheme);
    scheme->clone(*this, destDir);
}

std::optional<std::filesystem::path> Input::getSourcePath() const
{
    assert(scheme);
    return scheme->getSourcePath(*this);
}

void Input::putFile(const CanonPath & path, std::string_view contents, std::optional<std::string> commitMsg) const
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
    return store.makeFixedOutputPath(
        getName(),
        FixedOutputInfo{
            .method = FileIngestionMethod::NixArchive,
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
        } catch (BadHash & e) {
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

Input InputScheme::applyOverrides(const Input & input, std::optional<std::string> ref, std::optional<Hash> rev) const
{
    if (ref)
        throw Error("don't know how to set branch/tag name of input '%s' to '%s'", input.to_string(), *ref);
    if (rev)
        throw Error("don't know how to set revision of input '%s' to '%s'", input.to_string(), rev->gitRev());
    return input;
}

std::optional<std::filesystem::path> InputScheme::getSourcePath(const Input & input) const
{
    return {};
}

void InputScheme::putFile(
    const Input & input, const CanonPath & path, std::string_view contents, std::optional<std::string> commitMsg) const
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

std::string publicKeys_to_string(const std::vector<PublicKey> & publicKeys)
{
    return ((nlohmann::json) publicKeys).dump();
}

} // namespace nix::fetchers

namespace nlohmann {

using namespace nix;

#ifndef DOXYGEN_SKIP

fetchers::PublicKey adl_serializer<fetchers::PublicKey>::from_json(const json & json)
{
    fetchers::PublicKey res = {};
    auto & obj = getObject(json);
    if (auto * type = optionalValueAt(obj, "type"))
        res.type = getString(*type);

    res.key = getString(valueAt(obj, "key"));

    return res;
}

void adl_serializer<fetchers::PublicKey>::to_json(json & json, const fetchers::PublicKey & p)
{
    json["type"] = p.type;
    json["key"] = p.key;
}

#endif

} // namespace nlohmann

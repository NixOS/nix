#include "fetchers.hh"
#include "store-api.hh"
#include "input-accessor.hh"
#include "source-path.hh"
#include "fetch-to-store.hh"

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
    if (input.getRev())
        input.locked = true;
    input.getRevCount();
    input.getLastModified();
    if (input.getNarHash())
        input.locked = true;
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

std::pair<StorePath, Input> Input::fetch(ref<Store> store) const
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
            return scheme->fetch(store, *this);
        } catch (Error & e) {
            e.addTrace({}, "while fetching the input '%s'", to_string());
            throw;
        }
    }();

    auto narHash = store->queryPathInfo(storePath)->narHash;
    input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

    if (auto prevNarHash = getNarHash()) {
        if (narHash != *prevNarHash)
            throw Error((unsigned int) 102, "NAR hash mismatch in input '%s' (%s), expected '%s', got '%s'",
                to_string(),
                store->printStorePath(storePath),
                prevNarHash->to_string(HashFormat::SRI, true),
                narHash.to_string(HashFormat::SRI, true));
    }

    if (auto prevLastModified = getLastModified()) {
        if (input.getLastModified() != prevLastModified)
            throw Error("'lastModified' attribute mismatch in input '%s', expected %d",
                input.to_string(), *prevLastModified);
    }

    if (auto prevRev = getRev()) {
        if (input.getRev() != prevRev)
            throw Error("'rev' attribute mismatch in input '%s', expected %s",
                input.to_string(), prevRev->gitRev());
    }

    if (auto prevRevCount = getRevCount()) {
        if (input.getRevCount() != prevRevCount)
            throw Error("'revCount' attribute mismatch in input '%s', expected %d",
                input.to_string(), *prevRevCount);
    }

    input.locked = true;

    return {std::move(storePath), input};
}

std::pair<ref<InputAccessor>, Input> Input::getAccessor(ref<Store> store) const
{
    try {
        return scheme->getAccessor(store, *this);
    } catch (Error & e) {
        e.addTrace({}, "while fetching the input '%s'", to_string());
        throw;
    }
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

std::pair<StorePath, Input> InputScheme::fetch(ref<Store> store, const Input & input)
{
    auto [accessor, input2] = getAccessor(store, input);
    auto storePath = fetchToStore(*store, SourcePath(accessor), input2.getName());
    return {storePath, input2};
}

std::pair<ref<InputAccessor>, Input> InputScheme::getAccessor(ref<Store> store, const Input & input) const
{
    throw UnimplementedError("InputScheme must implement fetch() or getAccessor()");
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

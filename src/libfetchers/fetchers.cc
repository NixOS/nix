#include "fetchers.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

std::unique_ptr<std::vector<std::shared_ptr<InputScheme>>> inputSchemes = nullptr;

void registerInputScheme(std::shared_ptr<InputScheme> && inputScheme)
{
    if (!inputSchemes) inputSchemes = std::make_unique<std::vector<std::shared_ptr<InputScheme>>>();
    inputSchemes->push_back(std::move(inputScheme));
}

Input Input::fromURL(const std::string & url)
{
    return fromURL(parseURL(url));
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

Input Input::fromURL(const ParsedURL & url)
{
    for (auto & inputScheme : *inputSchemes) {
        auto res = inputScheme->inputFromURL(url);
        if (res) {
            res->scheme = inputScheme;
            fixupInput(*res);
            return std::move(*res);
        }
    }

    throw Error("input '%s' is unsupported", url.url);
}

Input Input::fromAttrs(Attrs && attrs)
{
    for (auto & inputScheme : *inputSchemes) {
        auto res = inputScheme->inputFromAttrs(attrs);
        if (res) {
            res->scheme = inputScheme;
            fixupInput(*res);
            return std::move(*res);
        }
    }

    Input input;
    input.attrs = attrs;
    fixupInput(input);
    return input;
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

Attrs Input::toAttrs() const
{
    return attrs;
}

std::optional<CanonPath> Input::isRelative() const
{
    assert(scheme);
    return scheme->isRelative(*this);
}

bool Input::hasAllInfo() const
{
    return getNarHash() && scheme && scheme->hasAllInfo(*this);
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
    auto [storePath, input] = [&]() -> std::pair<StorePath, Input> {
        try {
            auto [accessor, input2] = getAccessor(store);

            // FIXME: add an optimisation for the case where the
            // accessor is an FSInputAccessor pointing to a store
            // path.
            auto source = sinkToSource([&, accessor{accessor}](Sink & sink) {
                accessor->dumpPath(CanonPath::root, sink);
            });

            auto storePath = store->addToStoreFromDump(*source, input2.getName());

            return {storePath, input2};
        } catch (Error & e) {
            e.addTrace({}, "while fetching the input '%s'", to_string());
            throw;
        }
    }();

    return {std::move(storePath), input};
}

void Input::checkLocks(Input & input) const
{
    #if 0
    auto narHash = store.queryPathInfo(storePath)->narHash;
    input.attrs.insert_or_assign("narHash", narHash.to_string(SRI, true));
    #endif

    if (auto prevNarHash = getNarHash()) {
        if (input.getNarHash() != prevNarHash)
            throw Error((unsigned int) 102, "NAR hash mismatch in input '%s', expected '%s'",
                to_string(), prevNarHash->to_string(SRI, true));
    }

    if (auto prevLastModified = getLastModified()) {
        if (input.getLastModified() != prevLastModified)
            throw Error("'lastModified' attribute mismatch in input '%s', expected %d",
                input.to_string(), *prevLastModified);
    }

    if (auto prevRevCount = getRevCount()) {
        if (input.getRevCount() != prevRevCount)
            throw Error("'revCount' attribute mismatch in input '%s', expected %d",
                input.to_string(), *prevRevCount);
    }

    // FIXME
    #if 0
    input.locked = true;

    assert(input.hasAllInfo());
    #endif
}

std::pair<ref<InputAccessor>, Input> Input::getAccessor(ref<Store> store) const
{
    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    try {
        auto [accessor, final] = scheme->getAccessor(store, *this);
        checkLocks(final);
        return {accessor, std::move(final)};
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

std::string Input::getType() const
{
    return getStrAttr(attrs, "type");
}

std::optional<Hash> Input::getNarHash() const
{
    if (auto s = maybeGetStrAttr(attrs, "narHash")) {
        auto hash = s->empty() ? Hash(htSHA256) : Hash::parseSRI(*s);
        if (hash.type != htSHA256)
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
            // Default to sha1 for backwards compatibility with existing flakes
            hash = Hash::parseAny(*s, htSHA1);
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

std::optional<std::string> Input::getFingerprint(ref<Store> store) const
{
    if (auto rev = getRev())
        return rev->gitRev();
    assert(scheme);
    return scheme->getFingerprint(store, *this);
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

}

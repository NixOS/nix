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
        input.immutable = true;
    input.getRevCount();
    input.getLastModified();
    if (input.getNarHash())
        input.immutable = true;
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

std::pair<Tree, Input> Input::fetch(ref<Store> store) const
{
    if (!scheme)
        throw Error("cannot fetch unsupported input '%s'", attrsToJSON(toAttrs()));

    /* The tree may already be in the Nix store, or it could be
       substituted (which is often faster than fetching from the
       original source). So check that. */
    if (hasAllInfo()) {
        try {
            auto storePath = computeStorePath(*store);

            store->ensurePath(storePath);

            debug("using substituted/cached input '%s' in '%s'",
                to_string(), store->printStorePath(storePath));

            auto actualPath = store->toRealPath(storePath);

            return {fetchers::Tree(std::move(actualPath), std::move(storePath)), *this};
        } catch (Error & e) {
            debug("substitution of input '%s' failed: %s", to_string(), e.what());
        }
    }

    auto [tree, input] = [&]() -> std::pair<Tree, Input> {
        try {
            return scheme->fetch(store, *this);
        } catch (Error & e) {
            e.addTrace({}, "while fetching the input '%s'", to_string());
            throw;
        }
    }();

    if (tree.actualPath == "")
        tree.actualPath = store->toRealPath(tree.storePath);

    auto narHash = store->queryPathInfo(tree.storePath)->narHash;
    input.attrs.insert_or_assign("narHash", narHash.to_string(SRI, true));

    if (auto prevNarHash = getNarHash()) {
        if (narHash != *prevNarHash)
            throw Error((unsigned int) 102, "NAR hash mismatch in input '%s' (%s), expected '%s', got '%s'",
                to_string(), tree.actualPath, prevNarHash->to_string(SRI, true), narHash.to_string(SRI, true));
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

    input.immutable = true;

    assert(input.hasAllInfo());

    return {std::move(tree), input};
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

void Input::markChangedFile(
    std::string_view file,
    std::optional<std::string> commitMsg) const
{
    assert(scheme);
    return scheme->markChangedFile(*this, file, commitMsg);
}

std::string Input::getName() const
{
    return maybeGetStrAttr(attrs, "name").value_or("source");
}

StorePath Input::computeStorePath(Store & store) const
{
    auto narHash = getNarHash();
    if (!narHash)
        throw Error("cannot compute store path for mutable input '%s'", to_string());
    return store.makeFixedOutputPath(FileIngestionMethod::Recursive, *narHash, getName());
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
    if (auto s = maybeGetStrAttr(attrs, "rev"))
        return Hash::parseAny(*s, htSHA1);
    return {};
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

ParsedURL InputScheme::toURL(const Input & input)
{
    throw Error("don't know how to convert input '%s' to a URL", attrsToJSON(input.attrs));
}

Input InputScheme::applyOverrides(
    const Input & input,
    std::optional<std::string> ref,
    std::optional<Hash> rev)
{
    if (ref)
        throw Error("don't know how to set branch/tag name of input '%s' to '%s'", input.to_string(), *ref);
    if (rev)
        throw Error("don't know how to set revision of input '%s' to '%s'", input.to_string(), rev->gitRev());
    return input;
}

std::optional<Path> InputScheme::getSourcePath(const Input & input)
{
    return {};
}

void InputScheme::markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg)
{
    assert(false);
}

void InputScheme::clone(const Input & input, const Path & destDir)
{
    throw Error("do not know how to clone input '%s'", input.to_string());
}

}

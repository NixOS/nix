#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "filetransfer.hh"
#include "registry.hh"

#include <ctime>
#include <iomanip>

namespace nix {

void emitTreeAttrs(
    EvalState & state,
    const fetchers::Tree & tree,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback)
{
    assert(input.isImmutable());

    state.mkAttrs(v, 8);

    auto storePath = state.store->printStorePath(
        state.store->makeFixedOutputPathFromCA(tree.storePath));

    mkString(*state.allocAttr(v, state.sOutPath), storePath, PathSet({storePath}));

    // FIXME: support arbitrary input attributes.

    auto narHash = input.getNarHash();
    assert(narHash);
    mkString(*state.allocAttr(v, state.symbols.create("narHash")),
        narHash->to_string(SRI, true));

    if (auto rev = input.getRev()) {
        mkString(*state.allocAttr(v, state.symbols.create("rev")), rev->gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")), rev->gitShortRev());
    } else if (emptyRevFallback) {
        // Backwards compat for `builtins.fetchGit`: dirty repos return an empty sha1 as rev
        auto emptyHash = Hash(htSHA1);
        mkString(*state.allocAttr(v, state.symbols.create("rev")), emptyHash.gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")), emptyHash.gitRev());
    }

    if (input.getType() == "git")
        mkBool(*state.allocAttr(v, state.symbols.create("submodules")), maybeGetBoolAttr(input.attrs, "submodules").value_or(false));

    if (auto revCount = input.getRevCount())
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *revCount);
    else if (emptyRevFallback)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), 0);

    if (auto lastModified = input.getLastModified()) {
        mkInt(*state.allocAttr(v, state.symbols.create("lastModified")), *lastModified);
        mkString(*state.allocAttr(v, state.symbols.create("lastModifiedDate")),
            fmt("%s", std::put_time(std::gmtime(&*lastModified), "%Y%m%d%H%M%S")));
    }

    v.attrs->sort();
}

std::string fixURI(std::string uri, EvalState &state)
{
    state.checkURI(uri);
    return uri.find("://") != std::string::npos ? uri : "file://" + uri;
}

void addURI(EvalState &state, fetchers::Attrs &attrs, Symbol name, std::string v)
{
    string n(name);
    attrs.emplace(name, n == "url" ? fixURI(v, state) : v);
}

static void fetchTree(
    EvalState &state,
    const Pos &pos,
    Value **args,
    Value &v,
    const std::optional<std::string> type,
    bool emptyRevFallback = false
) {
    fetchers::Input input;
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {
        state.forceAttrs(*args[0], pos);

        fetchers::Attrs attrs;

        for (auto & attr : *args[0]->attrs) {
            state.forceValue(*attr.value);
            if (attr.value->type == tPath || attr.value->type == tString)
                addURI(
                    state,
                    attrs,
                    attr.name,
                    state.coerceToString(*attr.pos, *attr.value, context, false, false)
                );
            else if (attr.value->type == tString)
                addURI(state, attrs, attr.name, attr.value->string.s);
            else if (attr.value->type == tBool)
                attrs.emplace(attr.name, fetchers::Explicit<bool>{attr.value->boolean});
            else if (attr.value->type == tInt)
                attrs.emplace(attr.name, attr.value->integer);
            else
                throw TypeError("fetchTree argument '%s' is %s while a string, Boolean or integer is expected",
                    attr.name, showType(*attr.value));
        }

        if (type)
            attrs.emplace("type", type.value());

        if (!attrs.count("type"))
            throw Error({
                .hint = hintfmt("attribute 'type' is missing in call to 'fetchTree'"),
                .errPos = pos
            });

        input = fetchers::Input::fromAttrs(std::move(attrs));
    } else {
        auto url = fixURI(state.coerceToString(pos, *args[0], context, false, false), state);

        if (type == "git") {
            fetchers::Attrs attrs;
            attrs.emplace("type", "git");
            attrs.emplace("url", url);
            input = fetchers::Input::fromAttrs(std::move(attrs));
        } else {
            input = fetchers::Input::fromURL(url);
        }
    }

    if (!evalSettings.pureEval && !input.isDirect())
        input = lookupInRegistries(state.store, input).first;

    if (evalSettings.pureEval && !input.isImmutable())
        throw Error("in pure evaluation mode, 'fetchTree' requires an immutable input, at %s", pos);

    auto [tree, input2] = input.fetch(state.store);

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);

    emitTreeAttrs(state, tree, input2, v, emptyRevFallback);
}

static void prim_fetchTree(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    settings.requireExperimentalFeature("flakes");
    fetchTree(state, pos, args, v, std::nullopt);
}

static RegisterPrimOp r("fetchTree", 1, prim_fetchTree);

static void fetch(EvalState & state, const Pos & pos, Value * * args, Value & v,
    const string & who, bool unpack, std::string name)
{
    std::optional<std::string> url;
    std::optional<Hash> expectedHash;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "sha256")
                expectedHash = newHashAllowEmpty(state.forceStringNoCtx(*attr.value, *attr.pos), htSHA256);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError({
                    .hint = hintfmt("unsupported argument '%s' to '%s'", attr.name, who),
                    .errPos = *attr.pos
                });
            }

        if (!url)
            throw EvalError({
                .hint = hintfmt("'url' argument required"),
                .errPos = pos
            });
    } else
        url = state.forceStringNoCtx(*args[0], pos);

    url = resolveUri(*url);

    state.checkURI(*url);

    if (name == "")
        name = baseNameOf(*url);

    if (evalSettings.pureEval && !expectedHash)
        throw Error("in pure evaluation mode, '%s' requires a 'sha256' argument", who);

    // try to substitute if we can
    if (settings.useSubstitutes && expectedHash) {
        auto substitutableStorePath = fetchers::trySubstitute(state.store,
            unpack ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat, *expectedHash, name);
        if (substitutableStorePath) {
            auto substitutablePath = state.store->toRealPath(*substitutableStorePath);
            if (state.allowedPaths)
                state.allowedPaths->insert(substitutablePath);

            mkString(v, substitutablePath, PathSet({substitutablePath}));
            return;
        }
    }

    auto storePath =
        unpack
        ? fetchers::downloadTarball(state.store, *url, name, (bool) expectedHash).first.storePath
        : fetchers::downloadFile(state.store, *url, name, (bool) expectedHash).storePath;

    auto path = state.store->toRealPath(state.store->makeFixedOutputPathFromCA(storePath));

    if (expectedHash) {
        auto hash = unpack
            ? state.store->queryPathInfo(storePath)->narHash
            : hashFile(htSHA256, path);
        if (hash != *expectedHash)
            throw Error((unsigned int) 102, "hash mismatch in file downloaded from '%s':\n  wanted: %s\n  got:    %s",
                *url, expectedHash->to_string(Base32, true), hash->to_string(Base32, true));
    }

    if (state.allowedPaths)
        state.allowedPaths->insert(path);

    mkString(v, path, PathSet({path}));
}

static void prim_fetchurl(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetch(state, pos, args, v, "fetchurl", false, "");
}

static void prim_fetchTarball(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetch(state, pos, args, v, "fetchTarball", true, "source");
}

static void prim_fetchGit(EvalState &state, const Pos &pos, Value **args, Value &v)
{
    fetchTree(state, pos, args, v, "git", true);
}

static RegisterPrimOp r2("__fetchurl", 1, prim_fetchurl);
static RegisterPrimOp r3("fetchTarball", 1, prim_fetchTarball);
static RegisterPrimOp r4("fetchGit", 1, prim_fetchGit);

}

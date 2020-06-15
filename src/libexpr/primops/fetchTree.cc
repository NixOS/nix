#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "filetransfer.hh"

#include <ctime>
#include <iomanip>

namespace nix {

void emitTreeAttrs(
    EvalState & state,
    const fetchers::Tree & tree,
    std::shared_ptr<const fetchers::Input> input,
    Value & v)
{
    state.mkAttrs(v, 8);

    auto storePath = state.store->printStorePath(tree.storePath);

    mkString(*state.allocAttr(v, state.sOutPath), storePath, PathSet({storePath}));

    assert(tree.info.narHash);
    mkString(*state.allocAttr(v, state.symbols.create("narHash")),
        tree.info.narHash.to_string(SRI, true));

    if (input->getRev()) {
        mkString(*state.allocAttr(v, state.symbols.create("rev")), input->getRev()->gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")), input->getRev()->gitShortRev());
    }

    if (tree.info.revCount)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *tree.info.revCount);

    if (tree.info.lastModified)
        mkString(*state.allocAttr(v, state.symbols.create("lastModified")),
            fmt("%s", std::put_time(std::gmtime(&*tree.info.lastModified), "%Y%m%d%H%M%S")));

    v.attrs->sort();
}

static void prim_fetchTree(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    settings.requireExperimentalFeature("flakes");

    std::shared_ptr<const fetchers::Input> input;
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {
        state.forceAttrs(*args[0], pos);

        fetchers::Attrs attrs;

        for (auto & attr : *args[0]->attrs) {
            state.forceValue(*attr.value);
            if (attr.value->type == tString)
                attrs.emplace(attr.name, attr.value->string.s);
            else if (attr.value->type == tBool)
                attrs.emplace(attr.name, attr.value->boolean);
            else
                throw TypeError("fetchTree argument '%s' is %s while a string or Boolean is expected",
                    attr.name, showType(*attr.value));
        }

        if (!attrs.count("type"))
            throw Error({
                .hint = hintfmt("attribute 'type' is missing in call to 'fetchTree'"),
                .nixCode = NixCode { .errPos = pos }
            });

        input = fetchers::inputFromAttrs(attrs);
    } else
        input = fetchers::inputFromURL(state.coerceToString(pos, *args[0], context, false, false));

    if (evalSettings.pureEval && !input->isImmutable())
        throw Error("in pure evaluation mode, 'fetchTree' requires an immutable input");

    // FIXME: use fetchOrSubstituteTree
    auto [tree, input2] = input->fetchTree(state.store);

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);

    emitTreeAttrs(state, tree, input2, v);
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
                    .nixCode = NixCode { .errPos = *attr.pos }
                });
            }

        if (!url)
            throw EvalError({
                .hint = hintfmt("'url' argument required"),
                .nixCode = NixCode { .errPos = pos }
            });
    } else
        url = state.forceStringNoCtx(*args[0], pos);

    url = resolveUri(*url);

    state.checkURI(*url);

    if (name == "")
        name = baseNameOf(*url);

    if (evalSettings.pureEval && !expectedHash)
        throw Error("in pure evaluation mode, '%s' requires a 'sha256' argument", who);

    auto storePath =
        unpack
        ? fetchers::downloadTarball(state.store, *url, name, (bool) expectedHash).storePath
        : fetchers::downloadFile(state.store, *url, name, (bool) expectedHash).storePath;

    auto path = state.store->toRealPath(storePath);

    if (expectedHash) {
        auto hash = unpack
            ? state.store->queryPathInfo(storePath)->narHash
            : hashFile(htSHA256, path);
        if (hash != *expectedHash)
            throw Error((unsigned int) 102, "hash mismatch in file downloaded from '%s':\n  wanted: %s\n  got:    %s",
                *url, expectedHash->to_string(Base32, true), hash.to_string(Base32, true));
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

static RegisterPrimOp r2("__fetchurl", 1, prim_fetchurl);
static RegisterPrimOp r3("fetchTarball", 1, prim_fetchTarball);

}

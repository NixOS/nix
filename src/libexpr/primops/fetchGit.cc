#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "hash.hh"
#include "fetchers.hh"
#include "url.hh"

namespace nix {

static void prim_fetchGit(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string url;
    std::optional<std::string> ref;
    std::optional<Hash> rev;
    std::string name = "source";
    bool fetchSubmodules = false;
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.coerceToString(*attr.pos, *attr.value, context, false, false);
            else if (n == "ref")
                ref = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "rev")
                rev = Hash(state.forceStringNoCtx(*attr.value, *attr.pos), htSHA1);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "submodules")
                fetchSubmodules = state.forceBool(*attr.value, *attr.pos);
            else
                throw EvalError({
                    .hint = hintfmt("unsupported argument '%s' to 'fetchGit'", attr.name),
                    .nixCode = NixCode { .errPos = *attr.pos }
                });
        }

        if (url.empty())
            throw EvalError({
                .hint = hintfmt("'url' argument required"),
                .nixCode = NixCode { .errPos = pos }
            });

    } else
        url = state.coerceToString(pos, *args[0], context, false, false);

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.checkURI(url);

    if (evalSettings.pureEval && !rev)
        throw Error("in pure evaluation mode, 'fetchGit' requires a Git revision");

    fetchers::Attrs attrs;
    attrs.insert_or_assign("type", "git");
    attrs.insert_or_assign("url", url.find("://") != std::string::npos ? url : "file://" + url);
    if (ref) attrs.insert_or_assign("ref", *ref);
    if (rev) attrs.insert_or_assign("rev", rev->gitRev());
    if (fetchSubmodules) attrs.insert_or_assign("submodules", true);
    auto input = fetchers::inputFromAttrs(attrs);

    // FIXME: use name?
    auto [tree, input2] = input->fetchTree(state.store);

    state.mkAttrs(v, 8);
    auto storePath = state.store->printStorePath(tree.storePath);
    mkString(*state.allocAttr(v, state.sOutPath), storePath, PathSet({storePath}));
    // Backward compatibility: set 'rev' to
    // 0000000000000000000000000000000000000000 for a dirty tree.
    auto rev2 = input2->getRev().value_or(Hash(htSHA1));
    mkString(*state.allocAttr(v, state.symbols.create("rev")), rev2.gitRev());
    mkString(*state.allocAttr(v, state.symbols.create("shortRev")), rev2.gitShortRev());
    // Backward compatibility: set 'revCount' to 0 for a dirty tree.
    mkInt(*state.allocAttr(v, state.symbols.create("revCount")),
        tree.info.revCount.value_or(0));
    mkBool(*state.allocAttr(v, state.symbols.create("submodules")), fetchSubmodules);
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);
}

static RegisterPrimOp r("fetchGit", 1, prim_fetchGit);

}

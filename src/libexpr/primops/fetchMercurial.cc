#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "url.hh"
#include "url-parts.hh"

namespace nix {

static void prim_fetchMercurial(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string url;
    std::optional<Hash> rev;
    std::optional<std::string> ref;
    std::string_view name = "source";
    PathSet context;

    state.forceValue(*args[0], pos);

    if (args[0]->type() == nAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            std::string_view n(attr.name);
            if (n == "url")
                url = state.coerceToString(*attr.pos, *attr.value, context, false, false).toOwned();
            else if (n == "rev") {
                // Ugly: unlike fetchGit, here the "rev" attribute can
                // be both a revision or a branch/tag name.
                auto value = state.forceStringNoCtx(*attr.value, *attr.pos);
                if (std::regex_match(value.begin(), value.end(), revRegex))
                    rev = Hash::parseAny(value, htSHA1);
                else
                    ref = value;
            }
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError({
                    .msg = hintfmt("unsupported argument '%s' to 'fetchMercurial'", attr.name),
                    .errPos = *attr.pos
                });
        }

        if (url.empty())
            throw EvalError({
                .msg = hintfmt("'url' argument required"),
                .errPos = pos
            });

    } else
        url = state.coerceToString(pos, *args[0], context, false, false).toOwned();

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.checkURI(url);

    if (evalSettings.pureEval && !rev)
        throw Error("in pure evaluation mode, 'fetchMercurial' requires a Mercurial revision");

    fetchers::Attrs attrs;
    attrs.insert_or_assign("type", "hg");
    attrs.insert_or_assign("url", url.find("://") != std::string::npos ? url : "file://" + url);
    attrs.insert_or_assign("name", std::string(name));
    if (ref) attrs.insert_or_assign("ref", *ref);
    if (rev) attrs.insert_or_assign("rev", rev->gitRev());
    auto input = fetchers::Input::fromAttrs(std::move(attrs));

    // FIXME: use name
    auto [tree, input2] = input.fetch(state.store);

    auto attrs2 = state.buildBindings(8);
    auto storePath = state.store->printStorePath(
        state.store->makeFixedOutputPathFromCA(tree.storePath));
    attrs2.alloc(state.sOutPath).mkString(storePath, {storePath});
    if (input2.getRef())
        attrs2.alloc("branch").mkString(*input2.getRef());
    // Backward compatibility: set 'rev' to
    // 0000000000000000000000000000000000000000 for a dirty tree.
    auto rev2 = input2.getRev().value_or(Hash(htSHA1));
    attrs2.alloc("rev").mkString(rev2.gitRev());
    attrs2.alloc("shortRev").mkString(rev2.gitRev().substr(0, 12));
    if (auto revCount = input2.getRevCount())
        attrs2.alloc("revCount").mkInt(*revCount);
    v.mkAttrs(attrs2);

    state.allowPath(state.store->makeFixedOutputPathFromCA(tree.storePath));
}

static RegisterPrimOp r_fetchMercurial("fetchMercurial", 1, prim_fetchMercurial);

}

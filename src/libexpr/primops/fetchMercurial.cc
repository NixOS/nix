#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers/fetchers.hh"
#include "fetchers/parse.hh"
#include "fetchers/regex.hh"

#include <regex>

namespace nix {

static void prim_fetchMercurial(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string url;
    std::optional<Hash> rev;
    std::optional<std::string> ref;
    std::string name = "source";
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.coerceToString(*attr.pos, *attr.value, context, false, false);
            else if (n == "rev") {
                // Ugly: unlike fetchGit, here the "rev" attribute can
                // be both a revision or a branch/tag name.
                auto value = state.forceStringNoCtx(*attr.value, *attr.pos);
                if (std::regex_match(value, fetchers::revRegex))
                    rev = Hash(value, htSHA1);
                else
                    ref = value;
            }
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError("unsupported argument '%s' to 'fetchMercurial', at %s", attr.name, *attr.pos);
        }

        if (url.empty())
            throw EvalError(format("'url' argument required, at %1%") % pos);

    } else
        url = state.coerceToString(pos, *args[0], context, false, false);

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.checkURI(url);

    if (evalSettings.pureEval && !rev)
        throw Error("in pure evaluation mode, 'fetchMercurial' requires a Mercurial revision");

    auto parsedUrl = fetchers::parseURL(
        url.find("://") != std::string::npos
        ? "hg+" + url
        : "hg+file://" + url);
    if (rev) parsedUrl.query.insert_or_assign("rev", rev->gitRev());
    if (ref) parsedUrl.query.insert_or_assign("ref", *ref);
    // FIXME: use name
    auto input = inputFromURL(parsedUrl);

    auto [tree, input2] = input->fetchTree(state.store);

    state.mkAttrs(v, 8);
    auto storePath = state.store->printStorePath(tree.storePath);
    mkString(*state.allocAttr(v, state.sOutPath), storePath, PathSet({storePath}));
    if (input2->getRef())
        mkString(*state.allocAttr(v, state.symbols.create("branch")), *input2->getRef());
    // Backward compatibility: set 'rev' to
    // 0000000000000000000000000000000000000000 for a dirty tree.
    auto rev2 = input2->getRev().value_or(Hash(htSHA1));
    mkString(*state.allocAttr(v, state.symbols.create("rev")), rev2.gitRev());
    mkString(*state.allocAttr(v, state.symbols.create("shortRev")), std::string(rev2.gitRev(), 0, 12));
    if (tree.info.revCount)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *tree.info.revCount);
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);
}

static RegisterPrimOp r("fetchMercurial", 1, prim_fetchMercurial);

}

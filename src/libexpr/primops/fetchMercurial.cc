#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"

namespace nix {

static void prim_fetchMercurial(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    std::string url;
    std::optional<Hash> rev;
    std::optional<std::string> ref;
    std::string_view name = "source";
    NixStringContext context;

    state.forceValue(*args[0], pos);

    if (args[0]->type() == nAttrs) {

        for (auto & attr : *args[0]->attrs()) {
            std::string_view n(state.symbols[attr.name]);
            if (n == "url")
                url = state
                          .coerceToString(
                              attr.pos,
                              *attr.value,
                              context,
                              "while evaluating the `url` attribute passed to builtins.fetchMercurial",
                              false,
                              false)
                          .toOwned();
            else if (n == "rev") {
                // Ugly: unlike fetchGit, here the "rev" attribute can
                // be both a revision or a branch/tag name.
                auto value = state.forceStringNoCtx(
                    *attr.value, attr.pos, "while evaluating the `rev` attribute passed to builtins.fetchMercurial");
                if (std::regex_match(value.begin(), value.end(), revRegex))
                    rev = Hash::parseAny(value, HashAlgorithm::SHA1);
                else
                    ref = value;
            } else if (n == "name")
                name = state.forceStringNoCtx(
                    *attr.value, attr.pos, "while evaluating the `name` attribute passed to builtins.fetchMercurial");
            else
                state.error<EvalError>("unsupported argument '%s' to 'fetchMercurial'", state.symbols[attr.name])
                    .atPos(attr.pos)
                    .debugThrow();
        }

        if (url.empty())
            state.error<EvalError>("'url' argument required").atPos(pos).debugThrow();

    } else
        url = state
                  .coerceToString(
                      pos,
                      *args[0],
                      context,
                      "while evaluating the first argument passed to builtins.fetchMercurial",
                      false,
                      false)
                  .toOwned();

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.checkURI(url);

    if (state.settings.pureEval && !rev)
        throw Error("in pure evaluation mode, 'fetchMercurial' requires a Mercurial revision");

    fetchers::Attrs attrs;
    attrs.insert_or_assign("type", "hg");
    attrs.insert_or_assign("url", url.find("://") != std::string::npos ? url : "file://" + url);
    attrs.insert_or_assign("name", std::string(name));
    if (ref)
        attrs.insert_or_assign("ref", *ref);
    if (rev)
        attrs.insert_or_assign("rev", rev->gitRev());
    auto input = fetchers::Input::fromAttrs(state.fetchSettings, std::move(attrs));

    auto [storePath, input2] = input.fetchToStore(state.store);

    auto attrs2 = state.buildBindings(8);
    state.mkStorePathString(storePath, attrs2.alloc(state.s.outPath));
    if (input2.getRef())
        attrs2.alloc("branch").mkString(*input2.getRef());
    // Backward compatibility: set 'rev' to
    // 0000000000000000000000000000000000000000 for a dirty tree.
    auto rev2 = input2.getRev().value_or(Hash(HashAlgorithm::SHA1));
    attrs2.alloc("rev").mkString(rev2.gitRev());
    attrs2.alloc("shortRev").mkString(rev2.gitRev().substr(0, 12));
    if (auto revCount = input2.getRevCount())
        attrs2.alloc("revCount").mkInt(*revCount);
    v.mkAttrs(attrs2);

    state.allowPath(storePath);
}

static RegisterPrimOp r_fetchMercurial({.name = "fetchMercurial", .arity = 1, .fun = prim_fetchMercurial});

} // namespace nix

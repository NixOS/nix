#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers/fetchers.hh"
#include "fetchers/registry.hh"

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
        tree.info.narHash.to_string(SRI));

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

        fetchers::Input::Attrs attrs;

        for (auto & attr : *args[0]->attrs) {
            state.forceValue(*attr.value);
            if (attr.value->type == tString)
                attrs.emplace(attr.name, attr.value->string.s);
            else
                throw TypeError("fetchTree argument '%s' is %s while a string is expected",
                    attr.name, showType(*attr.value));
        }

        if (!attrs.count("type"))
            throw Error("attribute 'type' is missing in call to 'fetchTree', at %s", pos);

        input = fetchers::inputFromAttrs(attrs);
    } else
        input = fetchers::inputFromURL(state.coerceToString(pos, *args[0], context, false, false));

    if (!evalSettings.pureEval && !input->isDirect())
        input = lookupInRegistries(state.store, input).first;

    if (evalSettings.pureEval && !input->isImmutable())
        throw Error("in pure evaluation mode, 'fetchTree' requires an immutable input");

    // FIXME: use fetchOrSubstituteTree
    auto [tree, input2] = input->fetchTree(state.store);

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);

    emitTreeAttrs(state, tree, input2, v);
}

static RegisterPrimOp r("fetchTree", 1, prim_fetchTree);

}

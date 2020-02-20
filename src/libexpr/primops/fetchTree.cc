#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers/fetchers.hh"
#include "fetchers/registry.hh"

namespace nix {

static void prim_fetchTree(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    settings.requireExperimentalFeature("fetch-tree");

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
                throw Error("unsupported attribute type");
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

    auto [tree, input2] = input->fetchTree(state.store);

    state.mkAttrs(v, 8);
    auto storePath = state.store->printStorePath(tree.storePath);
    mkString(*state.allocAttr(v, state.sOutPath), storePath, PathSet({storePath}));
    if (input2->getRev()) {
        mkString(*state.allocAttr(v, state.symbols.create("rev")), input2->getRev()->gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")), input2->getRev()->gitShortRev());
    }
    if (tree.info.revCount)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *tree.info.revCount);
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);
}

static RegisterPrimOp r("fetchTree", 1, prim_fetchTree);

}

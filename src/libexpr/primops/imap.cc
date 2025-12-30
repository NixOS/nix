#include "nix/expr/primops.hh"

namespace nix {

static void prim_imap(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto shift = state.forceInt(*args[0], pos, "while evaluating the first argument passed to 'builtins.imap'").value;
    Value & f = *args[1];
    Value & list = *args[2];

    state.forceList(list, pos, "while evaluating the third argument passed to 'builtins.imap'");

    if (list.listSize() == 0) {
        v = list;
        return;
    }

    auto outList = state.buildList(list.listSize());
    for (const auto & [n, v] : enumerate(outList)) {
        v = state.allocValue();
        auto vIdx = state.allocValue();
        vIdx->mkInt(n + shift);
        Value * args[] = {vIdx, list.listView()[n]};
        state.callFunction(f, args, *v, pos);
    }

    v.mkList(outList);
}

static RegisterPrimOp primop_imap(
    {.name = "__imap",
     .args = {"shift", "f", "list"},
     .doc = R"(
       Apply the function *f* to each element in the list *list*. The function *f* is called with two arguments: the index of the element (plus *shift*) and the element itself.

       For example,

       ```nix
       builtins.imap 1 (i: v: "${v}-${toString i}") ["a" "b"]
       ```

       evaluates to `[ "a-1" "b-2" ]`.
     )",
     .fun = prim_imap});

} // namespace nix

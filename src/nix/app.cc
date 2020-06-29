#include "installables.hh"
#include "store-api.hh"
#include "eval-inline.hh"

namespace nix {

App::App(EvalState & state, Value & vApp)
{
    state.forceAttrs(vApp);

    auto aType = vApp.attrs->need(state.sType);
    if (state.forceStringNoCtx(*aType.value, *aType.pos) != "app")
        throw Error("value does not have type 'app', at %s", *aType.pos);

    auto aProgram = vApp.attrs->need(state.symbols.create("program"));
    program = state.forceString(*aProgram.value, context, *aProgram.pos);

    // FIXME: check that 'program' is in the closure of 'context'.
    if (!state.store->isInStore(program))
        throw Error("app program '%s' is not in the Nix store", program);
}

App Installable::toApp(EvalState & state)
{
    return App(state, *toValue(state).first);
}

}

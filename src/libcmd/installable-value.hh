#pragma once

#include "installables.hh"

namespace nix {

struct App
{
    std::vector<DerivedPath> context;
    Path program;
    // FIXME: add args, sandbox settings, metadata, ...
};

struct UnresolvedApp
{
    App unresolved;
    App resolve(ref<Store> evalStore, ref<Store> store);
};

struct InstallableValue : Installable
{
    ref<EvalState> state;

    InstallableValue(ref<EvalState> state) : state(state) {}

    virtual std::pair<Value *, PosIdx> toValue(EvalState & state) = 0;

    /* Get a cursor to each value this Installable could refer to. However
       if none exists, throw exception instead of returning empty vector. */
    virtual std::vector<ref<eval_cache::AttrCursor>>
    getCursors(EvalState & state);

    /* Get the first and most preferred cursor this Installable could refer
       to, or throw an exception if none exists. */
    virtual ref<eval_cache::AttrCursor>
    getCursor(EvalState & state);

    UnresolvedApp toApp(EvalState & state);

    static InstallableValue & require(Installable & installable);
    static ref<InstallableValue> require(ref<Installable> installable);
};

}

#pragma once

#include "installables.hh"

namespace nix {

struct InstallableValue : Installable
{
    ref<EvalState> state;

    InstallableValue(ref<EvalState> state) : state(state) {}
};

}

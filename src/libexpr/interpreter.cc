#include "nix/expr/interpreter.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/environment/system.hh"

namespace nix {

Interpreter::Interpreter(ref<EvalState> evalState)
    : evalState(evalState)
{
}

bool Interpreter::isReadOnly() const
{
    return evalState->settings.readOnlyMode;
}

Store & Interpreter::getStore()
{
    return *evalState->systemEnvironment->store;
}

const fetchers::Settings & Interpreter::getFetchSettings()
{
    return evalState->fetchSettings;
}

} // namespace nix
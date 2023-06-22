#include "command-installable-value.hh"

namespace nix {

void InstallableValueCommand::run(ref<Store> store, ref<Installable> installable)
{
    auto installableValue = InstallableValue::require(installable);
    run(store, installableValue);
}

}

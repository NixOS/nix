#include "nix/cmd/command-installable-value.hh"
#include "nix/cmd/installable-value.hh"
#include "nix/cmd/installables.hh"
#include "nix/store/store-api.hh"
#include "nix/util/ref.hh"

namespace nix {

void InstallableValueCommand::run(ref<Store> store, ref<Installable> installable)
{
    auto installableValue = InstallableValue::require(installable);
    run(store, installableValue);
}

} // namespace nix

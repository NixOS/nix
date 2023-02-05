#include "command-installable-value.hh"

namespace nix {

InstallableValueCommand::InstallableValueCommand()
    : SourceExprCommand()
{
    expectArgs({
        .label = "installable",
        .optional = true,
        .handler = {&_installable},
        .completer = getCompleteInstallable(),
    });
}

void InstallableValueCommand::run(ref<Store> store, ref<Installable> installable)
{
    auto installableValue = InstallableValue::require(installable);
    run(store, installableValue);
}

}

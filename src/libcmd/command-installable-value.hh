#include "installable-value.hh"
#include "command.hh"

namespace nix {

struct InstallableValueCommand : InstallableCommand
{
    virtual void run(ref<Store> store, ref<InstallableValue> installable) = 0;

    void run(ref<Store> store, ref<Installable> installable) override;
};

}

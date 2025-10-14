#include "nix/expr/environment.hh"
#include "nix/expr/environment/system.hh"

namespace nix {

ref<Environment>
makeSystemEnvironment(const EvalSettings & settings, ref<Store> store, std::shared_ptr<Store> buildStore)
{
    return make_ref<SystemEnvironment>(settings, store, buildStore);
}

} // namespace nix

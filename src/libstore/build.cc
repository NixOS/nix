#include "nix/store/build.hh"
#include "nix/store/build-store.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/globals.hh"

namespace nix {

void Builder::anchor() {}

void BuildStore::anchor() {}

ref<Builder> getDefaultBuilder(ref<Store> store, std::shared_ptr<Store> evalStore)
{
    if (auto * rbs = dynamic_cast<BuildStore *>(store.get()))
        return rbs->getBuilder(std::move(evalStore));
    else
        return getLocalBuilder(std::move(store), std::move(evalStore));
}

ref<Builder> getLocalBuilder(ref<Store> store, std::shared_ptr<Store> evalStore)
{
    auto evalStoreRef = evalStore ? ref<Store>(std::move(evalStore)) : store;
    return make_ref<Worker>(std::move(store), std::move(evalStoreRef));
}

} // namespace nix

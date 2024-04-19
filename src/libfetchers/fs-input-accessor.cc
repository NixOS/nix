#include "fs-input-accessor.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"

namespace nix {

struct FSInputAccessor : InputAccessor, PosixSourceAccessor
{
    using PosixSourceAccessor::PosixSourceAccessor;
};

ref<InputAccessor> makeFSInputAccessor()
{
    return make_ref<FSInputAccessor>();
}

ref<InputAccessor> makeFSInputAccessor(std::filesystem::path root)
{
    return make_ref<FSInputAccessor>(std::move(root));
}

ref<InputAccessor> makeStorePathAccessor(
    ref<Store> store,
    const StorePath & storePath)
{
    // FIXME: should use `store->getFSAccessor()`
    auto root = std::filesystem::path { store->toRealPath(storePath) };
    auto accessor = makeFSInputAccessor(root);
    accessor->setPathDisplay(root.string());
    return accessor;
}

SourcePath getUnfilteredRootPath(CanonPath path)
{
    static auto rootFS = makeFSInputAccessor();
    return {rootFS, path};
}

}

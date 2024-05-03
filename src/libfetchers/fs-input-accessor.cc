#include "fs-input-accessor.hh"
#include "posix-source-accessor.hh"
#include "store-api.hh"

namespace nix {

ref<SourceAccessor> makeFSInputAccessor()
{
    return make_ref<PosixSourceAccessor>();
}

ref<SourceAccessor> makeFSInputAccessor(std::filesystem::path root)
{
    return make_ref<PosixSourceAccessor>(std::move(root));
}

ref<SourceAccessor> makeStorePathAccessor(
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

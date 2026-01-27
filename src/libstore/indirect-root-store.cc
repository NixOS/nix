#include "nix/store/indirect-root-store.hh"

namespace nix {

void IndirectRootStore::makeSymlink(const std::filesystem::path & link, const std::filesystem::path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(link.parent_path());

    /* Create the new symlink. */
    auto tempLink = std::filesystem::path(link) += fmt(".tmp-%1%-%2%", getpid(), rand());
    createSymlink(target, tempLink);

    /* Atomically replace the old one. */
    std::filesystem::rename(tempLink, link);
}

std::filesystem::path IndirectRootStore::addPermRoot(const StorePath & storePath, const std::filesystem::path & _gcRoot)
{
    auto gcRoot = canonPath(_gcRoot);

    if (isInStore(gcRoot.string()))
        throw Error(
            "creating a garbage collector root (%1%) in the Nix store is forbidden "
            "(are you running nix-build inside the store?)",
            PathFmt(gcRoot));

    /* Register this root with the garbage collector, if it's
       running. This should be superfluous since the caller should
       have registered this root yet, but let's be on the safe
       side. */
    addTempRoot(storePath);

    /* Don't clobber the link if it already exists and doesn't
       point to the Nix store. */
    if (pathExists(gcRoot) && (!std::filesystem::is_symlink(gcRoot) || !isInStore(readLink(gcRoot).string())))
        throw Error("cannot create symlink %1%; already exists", PathFmt(gcRoot));

    makeSymlink(gcRoot, printStorePath(storePath));
    addIndirectRoot(gcRoot);

    return gcRoot;
}

} // namespace nix

#include "nix/store/indirect-root-store.hh"

namespace nix {

void IndirectRootStore::makeSymlink(const Path & link, const Path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(std::filesystem::path(link).parent_path());

    /* Create the new symlink. */
    Path tempLink = fmt("%1%.tmp-%2%-%3%", link, getpid(), rand());
    createSymlink(target, tempLink);

    /* Atomically replace the old one. */
    std::filesystem::rename(tempLink, link);
}

Path IndirectRootStore::addPermRoot(const StorePath & storePath, const Path & _gcRoot)
{
    Path gcRoot(canonPath(_gcRoot));

    if (isInStore(gcRoot))
        throw Error(
            "creating a garbage collector root (%1%) in the Nix store is forbidden "
            "(are you running nix-build inside the store?)",
            gcRoot);

    /* Register this root with the garbage collector, if it's
       running. This should be superfluous since the caller should
       have registered this root yet, but let's be on the safe
       side. */
    addTempRoot(storePath);

    /* Don't clobber the link if it already exists and doesn't
       point to the Nix store. */
    if (pathExists(gcRoot) && (!std::filesystem::is_symlink(gcRoot) || !isInStore(readLink(gcRoot).string())))
        throw Error("cannot create symlink '%1%'; already exists", gcRoot);

    makeSymlink(gcRoot, printStorePath(storePath));
    addIndirectRoot(gcRoot);

    return gcRoot;
}

} // namespace nix

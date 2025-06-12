#include "nix/store/indirect-root-store.hh"
#include "nix/util/file-system.hh"

namespace nix {

void IndirectRootStore::makeSymlink(const Path & link, const Path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

    /* Retry loop for temporary symlink creation to handle race conditions */
    while (true) {
        Path tempLink = makeTempPath(dirOf(link), baseNameOf(link) + ".tmp");

        createSymlink(target, tempLink);

        /* Atomically replace the old one. */
        try {
            std::filesystem::rename(tempLink, link);
            break; /* Success! */
        } catch (std::filesystem::filesystem_error & e) {
            try {
                std::filesystem::remove(tempLink);
            } catch (...) {
                /* Ignore errors removing the temp link */
            }

            if (e.code() == std::errc::file_exists) {
                /* Race condition: another process created the same temp link.
                   Try again with a different name. */
                continue;
            }

            throw SysError("failed to create symlink '%1%' -> '%2%'", link, target);
        }
    }
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
    if (pathExists(gcRoot) && (!std::filesystem::is_symlink(gcRoot) || !isInStore(readLink(gcRoot))))
        throw Error("cannot create symlink '%1%'; already exists", gcRoot);

    makeSymlink(gcRoot, printStorePath(storePath));
    addIndirectRoot(gcRoot);

    return gcRoot;
}

}

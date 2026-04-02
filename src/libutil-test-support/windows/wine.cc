#include "nix/util/tests/wine.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"

namespace nix::windows {

bool wineSymlinksSupported(const std::filesystem::path & dir)
{
    auto dirFd = openDirectory(dir, FinalSymlink::Follow);
    if (!dirFd)
        throw NativeSysError("opening directory %s", PathFmt(dir));

    auto name = OsFilename::fromPathAsserting("wine-symlink-probe");
    bool supported = true;
    try {
        createFileSymlinkAt(dirFd.get(), name, OS_STR("target"));
    } catch (SystemError &) {
        supported = false;
    }
    /* Also cleans up the empty file that a failed attempt leaves behind. */
    deletePath(dir / name.path());
    return supported;
}

} // namespace nix::windows

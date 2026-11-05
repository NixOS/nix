#include "nix/util/tests/wine.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/system-error.hh"

namespace nix::windows {

bool wineSymlinksSupported(const std::filesystem::path & dir)
{
    auto dirFd = valueOr(
        openDirectory(dir, FinalSymlink::Follow),
        NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(dir)));

    auto name = OsFilename::fromPathAsserting("wine-symlink-probe");
    bool supported = true;
    try {
        createFileSymlinkAt(dirFd.get(), name, OS_STR("target"));
    } catch (SystemError &) {
        supported = false;
    }
    /* Also cleans up the empty file that a failed attempt leaves behind. */
    deletePathAt(dirFd.get(), name);
    return supported;
}

} // namespace nix::windows

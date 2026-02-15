#include "nix/util/descriptor-destination.hh"
#include "nix/util/error.hh"
#include "nix/util/util.hh"

namespace nix {

DescriptorDestination DescriptorDestination::open(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    if (!path.has_parent_path() || path.parent_path() == path) {
        /* Path is a root (e.g., "/" or "C:\"), open it directly as a directory. */
        auto dirFd = openDirectory(path, finalSymlink);
        if (!dirFd)
            throw NativeSysError("opening directory %s", PathFmt(path));
        return dirFd;
    }

    /* Since there is no parent, we know this is a real directory, and
       any other type of file, including merely a symlink (to a
       directory), so we might as well use `DontFollow`. */
    auto parentDir = openDirectory(path.parent_path(), FinalSymlink::DontFollow);
    if (!parentDir)
        throw NativeSysError("opening parent directory of %s", PathFmt(path));
    return Parent{std::move(parentDir), path.filename()};
}

std::filesystem::path DescriptorDestination::toPath() const
{
    return std::visit(
        overloaded{
            [](const Parent & parent) { return descriptorToPath(parent.fd.get()) / parent.name; },
            [](const AutoCloseFD & dirFd) { return descriptorToPath(dirFd.get()); },
        },
        raw);
}

} // namespace nix

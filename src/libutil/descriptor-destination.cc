#include "nix/util/descriptor-destination.hh"
#include "nix/util/error.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/os-string.hh"
#include "nix/util/util.hh"

namespace nix {

/**
 * Internal helper that takes ownership of the directory fd.
 */
static DescriptorDestination
openAtOwned(AutoCloseFD dirFd, const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    assert(path.is_relative());

    // If path is empty, just return the directory fd
    if (path.empty()) {
        return std::move(dirFd);
    }

    {
        auto parentPath = path.parent_path();

        // For multi-component paths, recur with the lexical parent directory first so we
        // just have one component left.
        if (!parentPath.empty()) {
            auto result = openDirectoryAt(dirFd.get(), parentPath);
            if (!result)
                throw SystemError(
                    result.error(), "opening directory '%s'", PathFmt(descriptorToPath(dirFd.get()) / parentPath));
            return openAtOwned(std::move(result.value()), path.filename(), finalSymlink);
        }
    }

    assert(path == path.filename());
    assert(!path.empty() && path != "." && path != "..");

    // Single component path with NoFollow: just return Parent
    if (finalSymlink != FinalSymlink::Follow) {
        return DescriptorDestination::Parent{std::move(dirFd), path};
    }

    if (auto st = maybeFstatat(dirFd.get(), path); !st || !S_ISLNK(st->st_mode)) {
        // If the file at the end doesn't exist, or it does exist and is not a
        // symlink, then we know the parent of the path is in fact the real,
        // and not just lexical parent.
        return DescriptorDestination::Parent{std::move(dirFd), path};
    }

    auto target = readLinkAt(dirFd.get(), path);
    auto targetPath = std::filesystem::path(target);

    if (targetPath.is_absolute()) {
        // mutually recur (!)
        return DescriptorDestination::open(targetPath, finalSymlink);
    } else {
        // Recur to check if the target filename is also a symlink
        return openAtOwned(std::move(dirFd), targetPath, finalSymlink);
    }
}

DescriptorDestination
DescriptorDestination::openAt(Descriptor dirFd, const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    // Dup the borrowed descriptor to get ownership
    return openAtOwned(dupDescriptor(dirFd), path, finalSymlink);
}

DescriptorDestination DescriptorDestination::open(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    auto parentPath = path.parent_path();

    if (!path.has_parent_path() || parentPath == path) {
        /* Path is a root (e.g., "/" or "C:\"), open it directly as a directory.

           Since there is no parent, we know this is a real directory,
           and any other type of file, including merely a symlink (to a
           directory), so we might as well use `DontFollow`. */
        auto dirFd = openDirectory(path, FinalSymlink::DontFollow);
        if (!dirFd)
            throw NativeSysError("opening directory %s", PathFmt(path));
        return dirFd;
    }

    /* This is not the final part of the path anyways, so we always
       follow */
    auto parentDir = openDirectory(parentPath, FinalSymlink::Follow);
    if (!parentDir)
        throw NativeSysError("opening lexical parent directory %s of %s", PathFmt(parentPath), PathFmt(path));

    // Use openAtOwned directly since we already have ownership of parentDir
    return openAtOwned(std::move(parentDir), path.filename(), finalSymlink);
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

#include "nix/util/file-system-at.hh"

namespace nix {

std::optional<PosixStat> maybeFstatat(Descriptor dirFd, const CanonPath & path)
{
    try {
        return fstatat(dirFd, path);
    } catch (SystemError & e) {
        if (e.is(std::errc::no_such_file_or_directory) || e.is(std::errc::not_a_directory))
            return std::nullopt;
        throw;
    }
}

} // namespace nix

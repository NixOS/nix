#ifdef __APPLE__

#include "darwin-codesign.hh"
#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/util/processes.hh"

#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mach-o/loader.h>
#include <mach-o/fat.h>

namespace nix {

bool isMachOBinary(const std::filesystem::path & path)
{
    auto st = maybeLstat(path.c_str());
    if (!st || !S_ISREG(st->st_mode))
        return false;

    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        return false;

    uint32_t magic = 0;
    bool result = false;

    if (read(fd, &magic, sizeof(magic)) == sizeof(magic)) {
        result = magic == MH_MAGIC || magic == MH_MAGIC_64 ||
                 magic == MH_CIGAM || magic == MH_CIGAM_64 ||
                 magic == FAT_MAGIC || magic == FAT_CIGAM;
    }

    close(fd);
    return result;
}

/**
 * Remove the code signature from a Mach-O binary using codesign --remove-signature.
 * This properly removes the signature structure so the binary can be re-signed later.
 *
 * Previously we tried to zero the signature bytes manually, but this left the
 * LC_CODE_SIGNATURE load command pointing to zeroed data, creating an unparseable
 * signature blob that codesign -f -s - could not replace.
 */
void removeMachOCodeSignature(const std::filesystem::path & path)
{
    if (!isMachOBinary(path))
        return;

    try {
        auto result = runProgram(RunOptions{
            .program = "/usr/bin/codesign",
            .args = {"--remove-signature", path.string()},
        });

        if (!statusOk(result.first)) {
            debug("codesign --remove-signature failed for %s: %s", path, result.second);
        } else {
            debug("removed code signature from %s", path);
        }
    } catch (std::exception & e) {
        debug("failed to remove signature from %s: %s", path, e.what());
    }
}

void signMachOBinary(const std::filesystem::path & path)
{
    if (!isMachOBinary(path))
        return;

    try {
        // Use ad-hoc signing with codesign
        // -f: force (replace existing signature)
        // -s -: ad-hoc signing (no identity)
        auto result = runProgram(RunOptions{
            .program = "/usr/bin/codesign",
            .args = {"-f", "-s", "-", path.string()},
        });

        if (!statusOk(result.first)) {
            debug("codesign failed for %s: %s", path, result.second);
        } else {
            debug("signed %s with ad-hoc signature", path);
        }
    } catch (std::exception & e) {
        debug("failed to sign %s: %s", path, e.what());
    }
}

void removeMachOCodeSignaturesRecursively(const std::filesystem::path & path)
{
    auto st = maybeLstat(path.c_str());
    if (!st)
        return;

    if (S_ISREG(st->st_mode)) {
        if (isMachOBinary(path)) {
            removeMachOCodeSignature(path);
        }
    } else if (S_ISDIR(st->st_mode)) {
        for (auto & entry : std::filesystem::directory_iterator(path)) {
            removeMachOCodeSignaturesRecursively(entry.path());
        }
    }
    // Skip symlinks - they don't contain code signatures
}

void signMachOBinariesRecursively(const std::filesystem::path & path)
{
    auto st = maybeLstat(path.c_str());
    if (!st)
        return;

    if (S_ISREG(st->st_mode)) {
        if (isMachOBinary(path)) {
            signMachOBinary(path);
        }
    } else if (S_ISDIR(st->st_mode)) {
        for (auto & entry : std::filesystem::directory_iterator(path)) {
            signMachOBinariesRecursively(entry.path());
        }
    }
    // Skip symlinks
}

} // namespace nix

#endif // __APPLE__

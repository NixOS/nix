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
 * Process a single Mach-O slice (non-fat binary or one slice of a fat binary).
 * Returns true if a code signature was zeroed.
 */
static bool zeroMachOSliceSignature(void * base, size_t size, size_t offset)
{
    if (size < sizeof(mach_header))
        return false;

    auto * header = reinterpret_cast<mach_header *>(static_cast<char *>(base) + offset);

    bool is64 = false;
    bool swap = false;
    uint32_t magic = header->magic;

    if (magic == MH_MAGIC) {
        is64 = false;
        swap = false;
    } else if (magic == MH_MAGIC_64) {
        is64 = true;
        swap = false;
    } else if (magic == MH_CIGAM) {
        is64 = false;
        swap = true;
    } else if (magic == MH_CIGAM_64) {
        is64 = true;
        swap = true;
    } else {
        return false;
    }

    uint32_t ncmds = swap ? __builtin_bswap32(header->ncmds) : header->ncmds;
    uint32_t headerSize = is64 ? sizeof(mach_header_64) : sizeof(mach_header);

    if (offset + headerSize > size)
        return false;

    auto * cmd = reinterpret_cast<load_command *>(static_cast<char *>(base) + offset + headerSize);

    for (uint32_t i = 0; i < ncmds; i++) {
        uint32_t cmdType = swap ? __builtin_bswap32(cmd->cmd) : cmd->cmd;
        uint32_t cmdSize = swap ? __builtin_bswap32(cmd->cmdsize) : cmd->cmdsize;

        if (cmdType == LC_CODE_SIGNATURE) {
            auto * sigCmd = reinterpret_cast<linkedit_data_command *>(cmd);
            uint32_t dataoff = swap ? __builtin_bswap32(sigCmd->dataoff) : sigCmd->dataoff;
            uint32_t datasize = swap ? __builtin_bswap32(sigCmd->datasize) : sigCmd->datasize;

            // Zero out the signature data
            if (offset + dataoff + datasize <= size) {
                memset(static_cast<char *>(base) + offset + dataoff, 0, datasize);
                debug("zeroed %u bytes of code signature at offset %u", datasize, dataoff);
                return true;
            }
        }

        cmd = reinterpret_cast<load_command *>(reinterpret_cast<char *>(cmd) + cmdSize);
    }

    return false;
}

void zeroMachOCodeSignature(const std::filesystem::path & path)
{
    auto st = maybeLstat(path.c_str());
    if (!st || !S_ISREG(st->st_mode))
        return;

    int fd = open(path.c_str(), O_RDWR);
    if (fd == -1) {
        debug("cannot open %s for code signature zeroing: %s", path, strerror(errno));
        return;
    }

    auto cleanup = [&]() { close(fd); };

    size_t fileSize = st->st_size;
    if (fileSize < sizeof(uint32_t)) {
        cleanup();
        return;
    }

    void * mapped = mmap(nullptr, fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        debug("cannot mmap %s: %s", path, strerror(errno));
        cleanup();
        return;
    }

    auto unmapCleanup = [&]() {
        munmap(mapped, fileSize);
        cleanup();
    };

    uint32_t magic = *static_cast<uint32_t *>(mapped);

    if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
        // Fat binary - process each slice
        bool swap = (magic == FAT_CIGAM);
        auto * fatHeader = static_cast<fat_header *>(mapped);
        uint32_t nfat = swap ? __builtin_bswap32(fatHeader->nfat_arch) : fatHeader->nfat_arch;

        auto * arch = reinterpret_cast<fat_arch *>(fatHeader + 1);
        for (uint32_t i = 0; i < nfat; i++) {
            uint32_t archOffset = swap ? __builtin_bswap32(arch[i].offset) : arch[i].offset;
            zeroMachOSliceSignature(mapped, fileSize, archOffset);
        }
    } else if (magic == MH_MAGIC || magic == MH_MAGIC_64 ||
               magic == MH_CIGAM || magic == MH_CIGAM_64) {
        // Single-arch binary
        zeroMachOSliceSignature(mapped, fileSize, 0);
    }

    unmapCleanup();
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

void zeroMachOCodeSignaturesRecursively(const std::filesystem::path & path)
{
    auto st = maybeLstat(path.c_str());
    if (!st)
        return;

    if (S_ISREG(st->st_mode)) {
        if (isMachOBinary(path)) {
            zeroMachOCodeSignature(path);
        }
    } else if (S_ISDIR(st->st_mode)) {
        for (auto & entry : std::filesystem::directory_iterator(path)) {
            zeroMachOCodeSignaturesRecursively(entry.path());
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

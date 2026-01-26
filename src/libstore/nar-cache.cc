#include "nix/store/nar-cache.hh"
#include "nix/util/file-system.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

NarCache::NarCache(std::filesystem::path cacheDir_)
    : cacheDir(std::move(cacheDir_))
{
    assert(!cacheDir.empty());
    createDirs(cacheDir);
}

std::filesystem::path NarCache::makeCacheFile(const Hash & narHash, const std::string & ext)
{
    return (cacheDir / narHash.to_string(HashFormat::Nix32, false)) + "." + ext;
}

void NarCache::upsertNar(const Hash & narHash, Source & source)
{
    try {
        /* FIXME: do this asynchronously. */
        writeFile(makeCacheFile(narHash, "nar"), source);
    } catch (SystemError &) {
        ignoreExceptionExceptInterrupt();
    }
}

void NarCache::upsertNarListing(const Hash & narHash, std::string_view narListingData)
{
    try {
        writeFile(makeCacheFile(narHash, "ls"), narListingData);
    } catch (SystemError &) {
        ignoreExceptionExceptInterrupt();
    }
}

std::optional<std::string> NarCache::getNar(const Hash & narHash)
{
    try {
        return nix::readFile(makeCacheFile(narHash, "nar"));
    } catch (SystemError &) {
        return std::nullopt;
    }
}

GetNarBytes NarCache::getNarBytes(const Hash & narHash)
{
    return seekableGetNarBytes(makeCacheFile(narHash, "nar"));
}

std::optional<std::string> NarCache::getNarListing(const Hash & narHash)
{
    try {
        return nix::readFile(makeCacheFile(narHash, "ls"));
    } catch (SystemError &) {
        return std::nullopt;
    }
}

} // namespace nix

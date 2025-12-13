#include "nix/store/substitution-lock.hh"
#include "nix/util/users.hh"
#include "nix/util/hash.hh"
#include "nix/util/logging.hh"

#include <chrono>
#include <filesystem>
#include <mutex>

namespace nix {

/**
 * Clean up stale lock files older than maxAge.
 * This prevents accumulation of lock files after crashes.
 * Errors are ignored since cleanup is an optimization.
 */
static void
cleanupStaleLockFiles(const std::filesystem::path & lockDir, std::chrono::hours maxAge = std::chrono::hours(24))
{
    try {
        auto now = std::filesystem::file_time_type::clock::now();
        for (auto & entry : std::filesystem::directory_iterator(lockDir)) {
            try {
                if (entry.path().extension() == ".lock") {
                    auto mtime = entry.last_write_time();
                    auto age = now - mtime;
                    if (age > maxAge) {
                        debug("removing stale lock file '%s'", entry.path().string());
                        std::filesystem::remove(entry.path());
                    }
                }
            } catch (...) {
                /* Ignore errors for individual files - may be in use */
            }
        }
    } catch (...) {
        /* Ignore errors during cleanup - not critical */
    }
}

Path getSubstitutionLockPath(std::string_view hashPart)
{
    static std::once_flag substitutionLockDirCreated;
    auto lockDir = getCacheDir() + "/substitution-locks";
    std::call_once(substitutionLockDirCreated, [&]() {
        createDirs(lockDir);
        /* Periodically clean up stale lock files on startup */
        cleanupStaleLockFiles(lockDir);
    });
    /* Use the hash part directly as the lock file name since it's already
       a unique identifier for the store path. We add ".lock" extension for clarity. */
    return lockDir + "/" + std::string(hashPart) + ".lock";
}

} // namespace nix

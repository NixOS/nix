#include <filesystem>
#include <set>
#include <map>
#include <functional>

namespace nix::roots_tracer {
namespace fs = std::filesystem;
using std::set, std::map, std::string;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline void logNone(std::string_view)
{ }

struct TracerConfig {
    const fs::path storeDir = "/nix/store";
    const fs::path stateDir = "/nix/var/nix";
    const fs::path socketPath = "/nix/var/nix/gc-socket/socket";

    std::function<void(std::string_view msg)> log = logNone;
    std::function<void(std::string_view msg)> debug = logNone;
};

/**
 * A value of type `Roots` is a mapping from a store path to the set of roots that keep it alive
 */
typedef map<fs::path, std::set<fs::path>> Roots;

struct TraceResult {
    Roots storeRoots;
    set<fs::path> deadLinks;
};

/**
 * Return the set of all the store paths that are reachable from the given set
 * of filesystem paths, by:
 * - descending into the directories
 * - following the symbolic links (at most twice)
 * - reading the name of regular files (when encountering a file
 *   `/foo/bar/abcdef`, the algorithm will try to access `/nix/store/abcdef`)
 *
 * Also returns the set of all dead links encountered during the process (so
 * that they can be removed if it makes sense).
 */
TraceResult traceStaticRoots(TracerConfig opts, set<fs::path> initialRoots);

Roots getRuntimeRoots(TracerConfig opts);

}

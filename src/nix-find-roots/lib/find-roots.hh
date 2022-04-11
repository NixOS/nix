#include <filesystem>
#include <set>
#include <map>
#include <functional>

namespace nix::roots_tracer {
namespace fs = std::filesystem;
using std::set, std::map, std::string;

class Error : public std::exception {
private:
    const string message;

public:
    Error(std::string message)
        : message(message)
    {}

    const char* what() const noexcept override
    {
        return message.c_str();
    }
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

/*
 * A value of type `Roots` is a mapping from a store path to the set of roots that keep it alive
 */
typedef map<fs::path, std::set<fs::path>> Roots;
struct TraceResult {
    Roots storeRoots;
    set<fs::path> deadLinks;
};

TraceResult traceStaticRoots(TracerConfig opts, set<fs::path> initialRoots);
Roots getRuntimeRoots(TracerConfig opts);
}

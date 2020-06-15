#include "serialise.hh"
#include "store-api.hh"

namespace nix::daemon {

enum TrustedFlag : bool { NotTrusted = false, Trusted = true };
enum RecursiveFlag : bool { NotRecursive = false, Recursive = true };

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    TrustedFlag trusted,
    RecursiveFlag recursive,
    std::string_view userName,
    uid_t userId);

}

#include "serialise.hh"
#include "store-api.hh"

namespace nix::daemon {

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    bool trusted,
    const std::string & userName,
    uid_t userId);

}

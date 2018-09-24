#include "serialise.hh"

namespace nix::daemon {

void processConnection(
    FdSource & from,
    FdSink & to,
    bool trusted,
    const std::string & userName,
    uid_t userId);

}

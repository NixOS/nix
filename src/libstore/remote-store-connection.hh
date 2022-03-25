#include "remote-store.hh"
#include "worker-protocol.hh"

namespace nix {

struct RemoteStore::Connection
{
    FdSink to;
    FdSource from;
    unsigned int daemonVersion;
    std::optional<std::string> daemonNixVersion;
    std::chrono::time_point<std::chrono::steady_clock> startTime;

    operator worker_proto::ReadConn ()
    {
        return worker_proto::ReadConn {
            {
                .from = from,
            },
            .version = daemonVersion,
        };
    }

    operator worker_proto::WriteConn ()
    {
        return worker_proto::WriteConn {
            {
                .to = to,
            },
            .version = daemonVersion,
        };
    }

    virtual ~Connection();

    virtual void closeWrite() = 0;

    std::exception_ptr processStderr(Sink * sink = 0, Source * source = 0, bool flush = true);
};

}

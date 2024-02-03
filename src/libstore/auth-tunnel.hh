#pragma once

#include "file-descriptor.hh"
#include "worker-protocol.hh"

#include <thread>

namespace nix {

struct AuthTunnel
{
    AutoCloseFD clientFd, serverFd;
    std::thread serverThread;
    const WorkerProto::Version clientVersion;
    AuthTunnel(StoreDirConfig & storeConfig, WorkerProto::Version clientVersion);
    ~AuthTunnel();
};

namespace auth { struct AuthSource; }

ref<auth::AuthSource> makeTunneledAuthSource(
    ref<StoreDirConfig> storeConfig,
    WorkerProto::Version clientVersion,
    AutoCloseFD && clientFd);

}

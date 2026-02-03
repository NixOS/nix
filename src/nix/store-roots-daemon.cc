#include "nix/cmd/command.hh"
#include "nix/cmd/unix-socket-server.hh"
#include "nix/store/local-store.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-gc.hh"
#include "nix/util/file-descriptor.hh"

#include <thread>

using namespace nix;

struct CmdRootsDaemon : StoreConfigCommand
{
    CmdRootsDaemon() {}

    std::string description() override
    {
        return "run a daemon that returns garbage collector roots on request";
    }

    std::string doc() override
    {
        return
#include "store-roots-daemon.md"
            ;
    }

    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return Xp::LocalOverlayStore;
    }

    void run(ref<StoreConfig> storeConfig) override
    {
        auto localStoreConfig = dynamic_cast<LocalStoreConfig *>(&*storeConfig);
        if (!localStoreConfig) {
            throw UsageError(
                "Roots daemon only functions with a local store, not '%s'", storeConfig->getHumanReadableURI());
        }

        auto gcSocketPath = localStoreConfig->getRootsSocketPath();

        serveUnixSocket(
            {
                .socketPath = gcSocketPath,
                .socketMode = 0666,
            },
            [&](AutoCloseFD remote, std::function<void()> closeListeners) {
                std::thread([&, remote = std::move(remote)]() mutable {
                    auto roots = findRuntimeRootsUnchecked(*localStoreConfig);

                    FdSink sink(remote.get());

                    for (auto & [key, _] : roots) {
                        sink(localStoreConfig->printStorePath(key));
                        sink(std::string_view("\0", 1));
                    }

                    sink.flush();
                    remote.close();
                }).detach();
            });
    }
};

static auto rCmdStoreRootsDaemon = registerCommand2<CmdRootsDaemon>({"store", "roots-daemon"});

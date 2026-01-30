#include "nix/cmd/command.hh"
#include "nix/cmd/unix-socket-server.hh"
#include "nix/store/local-store.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-gc.hh"
#include "nix/util/configuration.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"

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

        unix::serveUnixSocket(
            {
                .socketPath = gcSocketPath,
                .socketMode = 0666,
            },
            [&](AutoCloseFD remote, std::function<void()> closeListeners) {
                unix::closeOnExec(remote.get());

                ProcessOptions options;
                options.runExitHandlers = true;
                options.allowVfork = false;

                startProcess([&, closeListeners = std::move(closeListeners)]() {
                    closeListeners();

                    auto roots = findRuntimeRootsUnchecked(*localStoreConfig);

                    FdSink sink(remote.get());

                    for (auto & [key, _] : roots) {
                        sink(localStoreConfig->printStorePath(key));
                        sink(std::string_view("\0", 1));
                    }

                    sink.flush();
                    remote.close();
                    exit(0);
                });
            });
    }
};

static auto rCmdStoreRootsDaemon = registerCommand2<CmdRootsDaemon>({"store", "roots-daemon"});

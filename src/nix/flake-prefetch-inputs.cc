#include "flake-command.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/util/thread-pool.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/exit.hh"

#include <nlohmann/json.hpp>

using namespace nix;
using namespace nix::flake;

struct CmdFlakePrefetchInputs : FlakeCommand
{
    std::string description() override
    {
        return "fetch the inputs of a flake";
    }

    std::string doc() override
    {
        return
#include "flake-prefetch-inputs.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = lockFlake();

        ThreadPool pool{fileTransferSettings.httpConnections};

        struct State
        {
            std::set<const Node *> done;
        };

        Sync<State> state_;

        std::atomic<size_t> nrFailed{0};

        auto visit = [&](this const auto & visit, const Node & node) {
            if (!state_.lock()->done.insert(&node).second)
                return;

            if (auto lockedNode = dynamic_cast<const LockedNode *>(&node)) {
                try {
                    Activity act(*logger, lvlInfo, actUnknown, fmt("fetching '%s'", lockedNode->lockedRef));
                    auto accessor = lockedNode->lockedRef.input.getAccessor(store).first;
                    fetchToStore(
                        fetchSettings, *store, accessor, FetchMode::Copy, lockedNode->lockedRef.input.getName());
                } catch (Error & e) {
                    printError("%s", e.what());
                    nrFailed++;
                }
            }

            for (auto & [inputName, input] : node.inputs) {
                if (auto inputNode = std::get_if<0>(&input))
                    pool.enqueue(std::bind(visit, **inputNode));
            }
        };

        pool.enqueue(std::bind(visit, *flake.lockFile.root));

        pool.process();

        throw Exit(nrFailed ? 1 : 0);
    }
};

static auto rCmdFlakePrefetchInputs = registerCommand2<CmdFlakePrefetchInputs>({"flake", "prefetch-inputs"});

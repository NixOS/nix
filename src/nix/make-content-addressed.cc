#include "command.hh"
#include "store-api.hh"
#include "make-content-addressed.hh"
#include "common-args.hh"
#include "json.hh"

using namespace nix;

struct CmdMakeContentAddressed : virtual CopyCommand, virtual StorePathsCommand, MixJSON
{
    CmdMakeContentAddressed()
    {
        realiseMode = Realise::Outputs;
    }

    std::string description() override
    {
        return "rewrite a path or closure to content-addressed form";
    }

    std::string doc() override
    {
        return
          #include "make-content-addressed.md"
          ;
    }

    void run(ref<Store> srcStore, StorePaths && storePaths) override
    {
        auto dstStore = dstUri.empty() ? openStore() : openStore(dstUri);

        auto remappings = makeContentAddressed(*srcStore, *dstStore,
            StorePathSet(storePaths.begin(), storePaths.end()));

        if (json) {
            JSONObject jsonRoot(std::cout);
            JSONObject jsonRewrites(jsonRoot.object("rewrites"));
            for (auto & path : storePaths) {
                auto i = remappings.find(path);
                assert(i != remappings.end());
                jsonRewrites.attr(srcStore->printStorePath(path), srcStore->printStorePath(i->second));
            }
        } else {
            for (auto & path : storePaths) {
                auto i = remappings.find(path);
                assert(i != remappings.end());
                notice("rewrote '%s' to '%s'",
                    srcStore->printStorePath(path),
                    srcStore->printStorePath(i->second));
            }
        }
    }
};

static auto rCmdMakeContentAddressed = registerCommand2<CmdMakeContentAddressed>({"store", "make-content-addressed"});

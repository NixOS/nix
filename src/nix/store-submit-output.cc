#include "nix/cmd/command.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/submit-store.hh"

namespace nix {

struct CmdSubmitOutput : StoreCommand
{
    std::string path;
    OutputName output;

    CmdSubmitOutput()
    {
        expectArg("path", &path);
        expectArg("output", &output);
    }

    std::string description() override
    {
        return "submit a store object as one of the outputs of the derivation currently being built";
    }

    std::string doc() override
    {
        return
#include "store-submit-output.md"
            ;
    }

    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return Xp::DynamicDerivations;
    }

    void run(ref<Store> store) override
    {
        auto & submitStore = require<SubmitStore>(*store);
        auto path = SingleDerivedPath::parse(*store, this->path);
        submitStore.submitOutput(path, output);
    }
};

static auto rCmdSubmitOutput = registerCommand2<CmdSubmitOutput>({"store", "submit-output"});

} // namespace nix

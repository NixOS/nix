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
        return "submit an output to the currently running derivation";
    }

    std::string doc() override
    {
        return
#include "store-submit-output.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto & submitStore = require<SubmitStore>(*store);
        auto path = SingleDerivedPath::parse(*store, this->path);
        submitStore.submitOutput(path, this->output);
    }
};

static auto rCmdSubmitOutput = registerCommand2<CmdSubmitOutput>({"store", "submit-output"});

} // namespace nix

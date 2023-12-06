#include "command.hh"

namespace nix {

struct ParseInstallableValueAdapter : ParseInstallableValueArgs
{
    ParseInstallableValueAdapter(GetRawInstallables & args)
        : MixRepair(args)
        , HasEvalState(args)
        , MixFlakeOptions(args)
        , ParseInstallableValueArgs(args)
    { }

    virtual ~ParseInstallableValueAdapter() = default;
};

ParseInstallableValueAdapter::RegisterDefault rParseInstallableValueAdapter {
    [](GetRawInstallables & args) -> ref<ParseInstallableArgs>
    {
        return make_ref<ParseInstallableValueAdapter>(args);
    }
};

}

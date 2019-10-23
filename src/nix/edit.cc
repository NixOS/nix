#include "command.hh"
#include "shared.hh"
#include "eval.hh"
#include "attr-path.hh"
#include "progress-bar.hh"

#include <unistd.h>

using namespace nix;

struct CmdEdit : InstallableCommand
{
    std::string name() override
    {
        return "edit";
    }

    std::string description() override
    {
        return "open the Nix expression of a Nix package in $EDITOR";
    }

    Examples examples() override
    {
        return {
            Example{
                "To open the Nix expression of the GNU Hello package:",
                "nix edit nixpkgs.hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        auto v = installable->toValue(*state);

        std::string filename;
        int lineno;
        std::tie(filename, lineno) = findDerivationFilename(*state, *v, installable->what());

        stopProgressBar();

        auto args = editorFor(filename, lineno);

        execvp(args.front().c_str(), stringsToCharPtrs(args).data());

        std::string command;
        for (const auto &arg : args) command += " '" + arg + "'";
        throw SysError("cannot run command%s", command);
    }
};

static RegisterCommand r1(make_ref<CmdEdit>());

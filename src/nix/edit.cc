#include "command.hh"
#include "shared.hh"
#include "eval.hh"
#include "attr-path.hh"
#include "progress-bar.hh"

#include <unistd.h>

using namespace nix;

struct CmdEdit : InstallableCommand
{
    std::string description() override
    {
        return "open the Nix expression of a Nix package in $EDITOR";
    }

    Examples examples() override
    {
        return {
            Example{
                "To open the Nix expression of the GNU Hello package:",
                "nix edit nixpkgs#hello"
            },
        };
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        auto [v, pos] = installable->toValue(*state);

        try {
            pos = findDerivationFilename(*state, *v, installable->what());
        } catch (NoPositionInfo &) {
        }

        if (pos == noPos)
            throw Error("cannot find position information for '%s", installable->what());

        stopProgressBar();

        auto args = editorFor(pos);

        restoreSignals();
        execvp(args.front().c_str(), stringsToCharPtrs(args).data());

        std::string command;
        for (const auto &arg : args) command += " '" + arg + "'";
        throw SysError("cannot run command%s", command);
    }
};

static auto r1 = registerCommand<CmdEdit>("edit");

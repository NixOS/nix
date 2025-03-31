#include "nix/current-process.hh"
#include "nix/command-installable-value.hh"
#include "nix/shared.hh"
#include "nix/eval.hh"
#include "nix/attr-path.hh"
#include "nix/editor-for.hh"

#include <unistd.h>

using namespace nix;

struct CmdEdit : InstallableValueCommand
{
    std::string description() override
    {
        return "open the Nix expression of a Nix package in $EDITOR";
    }

    std::string doc() override
    {
        return
          #include "edit.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store, ref<InstallableValue> installable) override
    {
        auto state = getEvalState();

        const auto [file, line] = [&] {
            auto [v, pos] = installable->toValue(*state);

            try {
                return findPackageFilename(*state, *v, installable->what());
            } catch (NoPositionInfo &) {
                throw Error("cannot find position information for '%s", installable->what());
            }
        }();

        logger->stop();

        auto args = editorFor(file, line);

        restoreProcessContext();

        execvp(args.front().c_str(), stringsToCharPtrs(args).data());

        std::string command;
        for (const auto &arg : args) command += " '" + arg + "'";
        throw SysError("cannot run command%s", command);
    }
};

static auto rCmdEdit = registerCommand<CmdEdit>("edit");

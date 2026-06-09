#include "nix/util/current-process.hh"
#include "nix/cmd/command-installable-value.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/attr-path.hh"
#include "nix/cmd/editor-for.hh"

#include <unistd.h>

namespace nix {

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

    Category category() override
    {
        return catSecondary;
    }

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

        auto [args, tempFd, delTemp] = editorFor(file, line, /*readOnly=*/true);
        auto program = args.front();
        args.pop_front();

        runProgram2(
            RunOptions{
                .program = program,
                .lookupPath = true,
                .args = std::move(args),
                .isInteractive = true,
            });
    }
};

static auto rCmdEdit = registerCommand<CmdEdit>("edit");

} // namespace nix

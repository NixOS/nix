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

        Value * v2;
        try {
            auto dummyArgs = state->allocBindings(0);
            v2 = findAlongAttrPath(*state, "meta.position", *dummyArgs, *v);
        } catch (Error &) {
            throw Error("package '%s' has no source location information", installable->what());
        }

        auto pos = state->forceString(*v2);
        debug("position is %s", pos);

        auto colon = pos.rfind(':');
        if (colon == std::string::npos)
            throw Error("cannot parse meta.position attribute '%s'", pos);

        std::string filename(pos, 0, colon);
        int lineno;
        try {
            lineno = std::stoi(std::string(pos, colon + 1));
        } catch (std::invalid_argument & e) {
            throw Error("cannot parse line number '%s'", pos);
        }

        stopProgressBar();

        auto args = editorFor(filename, lineno);

        execvp(args.front().c_str(), stringsToCharPtrs(args).data());

        std::string command;
        for (const auto &arg : args) command += " '" + arg + "'";
        throw SysError("cannot run command%s", command);
    }
};

static RegisterCommand r1(make_ref<CmdEdit>());

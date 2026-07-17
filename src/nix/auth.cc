#include "nix/util/auth.hh"
#include "nix/cmd/command.hh"

namespace nix {

struct CmdAuthFill : Command
{
    bool require = false;

    CmdAuthFill()
    {
        addFlag({
            .longName = "require",
            .description = "Prompt for credentials if no authentication source provides them.",
            .handler = {&require, true},
        });
    }

    std::string description() override
    {
        return "obtain a user name and password from the configured authentication sources";
    }

    std::string doc() override
    {
        return
#include "auth-fill.md"
            ;
    }

    Category category() override
    {
        return catUtility;
    }

    void run() override
    {
        logger->pause();
        auto request = auth::AuthData::parseGitAuthData(drainFD(STDIN_FILENO));
        if (auto authData = auth::getAuthenticator()->fill(request, require))
            writeFull(STDOUT_FILENO, authData->toGitAuthData());
    }
};

struct CmdAuth : NixMultiCommand
{
    CmdAuth()
        : NixMultiCommand("auth", RegisterCommand::getCommandsFor({"auth"}))
    {
    }

    std::string description() override
    {
        return "authentication-related commands";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdAuth = registerCommand<CmdAuth>("auth");
static auto rCmdAuthFill = registerCommand2<CmdAuthFill>({"auth", "fill"});

} // namespace nix

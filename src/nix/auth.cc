#include "auth.hh"
#include "command.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdAuthFill : Command
{
    bool require = false;

    CmdAuthFill()
    {
        addFlag({
            .longName = "require",
            .description = "Prompt the user for authentication if no authentication source provides it.",
            .handler = {&require, true},
        });
    }

    std::string description() override
    {
        return "obtain a user name and password from the configured authentication sources";
    }

    #if 0
    std::string doc() override
    {
        return
          #include "auth-fill.md"
          ;
    }
    #endif

    void run() override
    {
        using namespace auth;
        stopProgressBar();
        auto authRequest = AuthData::parseGitAuthData(drainFD(STDIN_FILENO));
        auto authData = getAuthenticator()->fill(authRequest, require);
        if (authData)
            writeFull(STDOUT_FILENO, authData->toGitAuthData());
    }
};

struct CmdAuth : NixMultiCommand
{
    CmdAuth()
        : NixMultiCommand(
            "auth",
            {
                {"fill", []() { return make_ref<CmdAuthFill>(); }},
            })
    {
    }

    std::string description() override
    {
        return "authentication-related commands";
    }

    Category category() override { return catUtility; }
};

static auto rCmdAuth = registerCommand<CmdAuth>("auth");

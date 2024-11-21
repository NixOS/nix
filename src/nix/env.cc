#include <unordered_set>
#include <queue>

#include "command.hh"
#include "eval.hh"
#include "run.hh"
#include "strings.hh"
#include "executable-path.hh"

using namespace nix;

struct CmdEnv : NixMultiCommand
{
    CmdEnv()
        : NixMultiCommand("env", RegisterCommand::getCommandsFor({"env"}))
    {
    }

    std::string description() override
    {
        return "manipulate the process environment";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdEnv = registerCommand<CmdEnv>("env");

struct CmdShell : InstallablesCommand, MixEnvironment
{

    using InstallablesCommand::run;

    std::vector<std::string> command = {getEnv("SHELL").value_or("bash")};

    CmdShell()
    {
        addFlag(
            {.longName = "command",
             .shortName = 'c',
             .description = "Command and arguments to be executed, defaulting to `$SHELL`",
             .labels = {"command", "args"},
             .handler = {[&](std::vector<std::string> ss) {
                 if (ss.empty())
                     throw UsageError("--command requires at least one argument");
                 command = ss;
             }}});
    }

    std::string description() override
    {
        return "run a shell in which the specified packages are available";
    }

    std::string doc() override
    {
        return
#include "shell.md"
            ;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        auto outPaths =
            Installable::toStorePaths(getEvalStore(), store, Realise::Outputs, OperateOn::Output, installables);

        auto accessor = store->getFSAccessor();

        std::unordered_set<StorePath> done;
        std::queue<StorePath> todo;
        for (auto & path : outPaths)
            todo.push(path);

        setEnviron();

        std::vector<std::string> pathAdditions;

        while (!todo.empty()) {
            auto path = todo.front();
            todo.pop();
            if (!done.insert(path).second)
                continue;

            if (true)
                pathAdditions.push_back(store->printStorePath(path) + "/bin");

            auto propPath = accessor->resolveSymlinks(
                CanonPath(store->printStorePath(path)) / "nix-support" / "propagated-user-env-packages");
            if (auto st = accessor->maybeLstat(propPath); st && st->type == SourceAccessor::tRegular) {
                for (auto & p : tokenizeString<Paths>(accessor->readFile(propPath)))
                    todo.push(store->parseStorePath(p));
            }
        }

        // TODO: split losslessly; empty means .
        auto unixPath = ExecutablePath::load();
        unixPath.directories.insert(unixPath.directories.begin(), pathAdditions.begin(), pathAdditions.end());
        auto unixPathString = unixPath.render();
        setEnvOs(OS_STR("PATH"), unixPathString.c_str());

        Strings args;
        for (auto & arg : command)
            args.push_back(arg);

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        getEvalState()->evalCaches.clear();

        execProgramInStore(store, UseLookupPath::Use, *command.begin(), args);
    }
};

static auto rCmdShell = registerCommand2<CmdShell>({"env", "shell"});

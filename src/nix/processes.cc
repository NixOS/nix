#include "command.hh"
#include "store-api.hh"
#include "pathlocks.hh"

#include <fcntl.h>

using namespace nix;

struct CmdProcesses : StoreCommand
{
    CmdProcesses()
    {
    }

    std::string description() override
    {
        return "show processes";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show what processes are currently building:",
                "nix processes"
            },
        };
    }

    Category category() override { return catSecondary; }

    static std::optional<std::string> getCmdline(int pid)
    {
        auto cmdlinePath = fmt("/proc/%d/cmdline", pid);
        if (pathExists(cmdlinePath)) {
            string cmdline = readFile(cmdlinePath);
            string cmdline_;
            for (auto & i : cmdline) {
                if (i == 0) cmdline_ += ' ';
                else cmdline_ += i;
            }
            return cmdline_;
        }
        return std::nullopt;
    }

    // fuser just checks /proc on Linux, so we could just do that instead of calling an external program
    // TODO: do this in C++ on Linux
    static int fuser(Path path)
    {
        int fds[2];
        if (pipe(fds) == -1)
            throw Error("failed to make fuser pipe");

        pid_t pid = fork();
        if (pid < 0)
            throw Error("failed to fork for fuser");

        if (pid == 0) {
            dup2(fds[1], fileno(stdout));
            dup2(open("/dev/null", 0), fileno(stderr));
            close(fds[0]); close(fds[1]);
            if (!execlp("fuser", "fuser", path.c_str(), NULL))
                throw Error("failed to execute program fuser");
        }

        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw Error("failed to execute fuser with status '%d'", status);
        char buffer[4096];
        ssize_t size = read(fds[0], &buffer, sizeof(buffer));
        return std::stoi(std::string(buffer, size));
    }

    void run(ref<Store> store) override
    {
        if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>()) {
            auto userPoolDir = store2->stateDir + "/userpool";

            struct stat st;
            stat(userPoolDir.c_str(), &st);
            if (st.st_uid != geteuid())
                throw Error("you don't have permissions to see the userpool locks");

            auto dirs = readDirectory(userPoolDir);
            for (auto i = dirs.begin(); i != dirs.end(); i++) {
                auto uid = i->name;
                auto uidPath = userPoolDir + "/" + uid;

                // try to lock it ourselves
                int fd = open(uidPath.c_str(), O_CLOEXEC | O_RDWR, 0600);
                if (lockFile(fd, ltWrite, false)) {
                    close(fd);
                    continue;
                }
                close(fd);

                int pid = fuser(uidPath);

                if (i != dirs.begin())
                    std::cout << std::endl;

                struct passwd * pw = getpwuid(std::stoi(uid));
                if (!pw)
                    throw Error("can't find uid for '%s'", uid);
                std::cout << fmt("Build User: %s (%d)", pw->pw_name, uid) << std::endl;

                if (auto cmdline = getCmdline(pid))
                    std::cout << fmt("Build Process: %s (%d)", *cmdline, pid) << std::endl;
                else
                    std::cout << fmt("Build Process: %d", pid) << std::endl;

                // TODO: print leaves of child process by searching for ppid in /proc
                // if (pathExists(fmt("/proc/%d/task", pid)))
                //     printLeafProcesses(pid);

                auto openFds = fmt("/proc/%d/fd", pid);
                if (pathExists(openFds))
                    for (auto & entry : readDirectory(openFds)) {
                        auto path = readLink(fmt("/proc/%d/fd/%s", pid, entry.name));
                        if (hasSuffix(path, ".lock"))
                            std::cout << fmt("File Lock: %s", path) << std::endl;
                    }
            }
        } else
            throw Error("must provide local store for nix process, found '%s'", store->getUri());
    }
};

static auto r1 = registerCommand<CmdProcesses>("processes");

#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/active-builds.hh"
#include "nix/util/table.hh"
#include "nix/util/terminal.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdPs : MixJSON, StoreCommand
{
    std::string description() override
    {
        return "list active builds";
    }

    Category category() override
    {
        return catUtility;
    }

    std::string doc() override
    {
        return
#include "ps.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto & tracker = require<QueryActiveBuildsStore>(*store);

        auto builds = tracker.queryActiveBuilds();

        if (json) {
            printJSON(nlohmann::json(builds));
            return;
        }

        if (builds.empty()) {
            notice("No active builds.");
            return;
        }

        /* Helper to format user info: show name if available, else UID */
        auto formatUser = [](const UserInfo & user) -> std::string {
            return user.name ? *user.name : std::to_string(user.uid);
        };

        Table table;

        /* Add column headers. */
        table.push_back({{"USER"}, {"PID"}, {"CPU", TableCell::Alignment::Right}, {"DERIVATION/COMMAND"}});

        for (const auto & build : builds) {
            /* Calculate CPU time - use cgroup stats if available, otherwise sum process times. */
            std::chrono::microseconds cpuTime = build.utime && build.stime ? *build.utime + *build.stime : [&]() {
                std::chrono::microseconds total{0};
                for (const auto & process : build.processes)
                    total += process.utime.value_or(std::chrono::microseconds(0))
                             + process.stime.value_or(std::chrono::microseconds(0))
                             + process.cutime.value_or(std::chrono::microseconds(0))
                             + process.cstime.value_or(std::chrono::microseconds(0));
                return total;
            }();

            /* Add build summary row. */
            table.push_back(
                {formatUser(build.mainUser),
                 std::to_string(build.mainPid),
                 {fmt("%.1fs",
                      std::chrono::duration_cast<std::chrono::duration<float, std::chrono::seconds::period>>(cpuTime)
                          .count()),
                  TableCell::Alignment::Right},
                 fmt(ANSI_BOLD "%s" ANSI_NORMAL " (wall=%ds)",
                     store->printStorePath(build.derivation),
                     time(nullptr) - build.startTime)});

            if (build.processes.empty()) {
                table.push_back(
                    {formatUser(build.mainUser),
                     std::to_string(build.mainPid),
                     {"", TableCell::Alignment::Right},
                     fmt("%s" ANSI_ITALIC "(no process info)" ANSI_NORMAL, treeLast)});
            } else {
                /* Recover the tree structure of the processes. */
                std::set<pid_t> pids;
                for (auto & process : build.processes)
                    pids.insert(process.pid);

                using Processes = std::set<const ActiveBuildInfo::ProcessInfo *>;
                std::map<pid_t, Processes> children;
                Processes rootProcesses;
                for (auto & process : build.processes) {
                    if (pids.contains(process.parentPid))
                        children[process.parentPid].insert(&process);
                    else
                        rootProcesses.insert(&process);
                }

                /* Render the process tree. */
                [&](this auto const & visit, const Processes & processes, std::string_view prefix) -> void {
                    for (const auto & [n, process] : enumerate(processes)) {
                        bool last = n + 1 == processes.size();

                        // Format CPU time if available
                        std::string cpuInfo;
                        if (process->utime || process->stime || process->cutime || process->cstime) {
                            auto totalCpu = process->utime.value_or(std::chrono::microseconds(0))
                                            + process->stime.value_or(std::chrono::microseconds(0))
                                            + process->cutime.value_or(std::chrono::microseconds(0))
                                            + process->cstime.value_or(std::chrono::microseconds(0));
                            auto totalSecs =
                                std::chrono::duration_cast<std::chrono::duration<float, std::chrono::seconds::period>>(
                                    totalCpu)
                                    .count();
                            cpuInfo = fmt("%.1fs", totalSecs);
                        }

                        // Format argv with tree structure
                        auto argv = concatStringsSep(
                            " ", tokenizeString<std::vector<std::string>>(concatStringsSep(" ", process->argv)));

                        table.push_back(
                            {formatUser(process->user),
                             std::to_string(process->pid),
                             {cpuInfo, TableCell::Alignment::Right},
                             fmt("%s%s%s", prefix, last ? treeLast : treeConn, argv)});

                        visit(children[process->pid], last ? prefix + treeNull : prefix + treeLine);
                    }
                }(rootProcesses, "");
            }
        }

        auto width = isTTY() && isatty(STDOUT_FILENO) ? getWindowWidth() : std::numeric_limits<unsigned int>::max();

        printTable(std::cout, table, width);
    }
};

static auto rCmdPs = registerCommand2<CmdPs>({"ps"});

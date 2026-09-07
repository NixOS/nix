#include "../local-gc-private.hh"

#include "nix/util/error.hh"
#include "nix/util/fmt.hh"
#include "nix/util/signals.hh"

#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>

namespace nix {

void findDarwinRuntimeRoots(const StoreDirConfig & config, UncheckedRoots & unchecked)
{
    auto storePathRegex = makeStorePathRegex(config);

    std::vector<int> pids;
    std::size_t pidBufSize = 1;

    while (pidBufSize > pids.size() * sizeof(int)) {
        // Reserve some extra size so we don't fail too much.
        pids.resize((pidBufSize + pidBufSize / 8) / sizeof(int));
        auto size = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), pids.size() * sizeof(int));

        if (size <= 0)
            throw SysError("listing PIDs");
        pidBufSize = size;
    }

    pids.resize(pidBufSize / sizeof(int));

    for (auto pid : pids) {
        checkInterrupt();

        // It doesn't make sense to ask about the kernel.
        if (pid == 0)
            continue;

        try {
            // Process cwd/root directory.
            struct proc_vnodepathinfo vnodeInfo;
            if (proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &vnodeInfo, sizeof(vnodeInfo)) <= 0)
                throw SysError("getting pid %1% working directory", pid);

            unchecked[std::string(vnodeInfo.pvi_cdir.vip_path)].emplace(fmt("{libproc/%d/cwd}", pid));
            unchecked[std::string(vnodeInfo.pvi_rdir.vip_path)].emplace(fmt("{libproc/%d/rootdir}", pid));

            // File descriptors.
            std::vector<struct proc_fdinfo> fds;
            std::size_t fdBufSize = 1;
            while (fdBufSize > fds.size() * sizeof(struct proc_fdinfo)) {
                // Reserve some extra size so we don't fail too much.
                fds.resize((fdBufSize + fdBufSize / 8) / sizeof(struct proc_fdinfo));
                errno = 0;
                auto size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), fds.size() * sizeof(struct proc_fdinfo));

                // The libproc wrapper converts a -1 syscall result to 0. A
                // process with no file descriptors also returns 0, but leaves
                // errno unchanged.
                if (size <= 0) {
                    if (errno == 0) {
                        fdBufSize = 0;
                        break;
                    }
                    throw SysError("listing pid %1% file descriptors", pid);
                }
                fdBufSize = size;
            }
            fds.resize(fdBufSize / sizeof(struct proc_fdinfo));

            for (auto fd : fds) {
                // By definition, only a vnode is on the filesystem.
                if (fd.proc_fdtype != PROX_FDTYPE_VNODE)
                    continue;

                struct vnode_fdinfowithpath fdInfo;
                if (proc_pidfdinfo(pid, fd.proc_fd, PROC_PIDFDVNODEPATHINFO, &fdInfo, sizeof(fdInfo)) <= 0) {
                    // They probably just closed this fd, so continue looking
                    // at ranges and environment variables.
                    if (errno == EBADF)
                        continue;
                    throw SysError("getting pid %1% fd %2% path", pid, fd.proc_fd);
                }

                unchecked[std::string(fdInfo.pvip.vip_path)].emplace(fmt("{libproc/%d/fd/%d}", pid, fd.proc_fd));
            }

            // Regions, including mmapped files, executables, and shared libraries.
            uint64_t nextAddr = 0;
            while (true) {
                // PROC_PIDREGIONPATHINFO2 includes regions backed by a vnode
                // and has been available since OS X 10.10, but is not exposed
                // by the SDK headers. Its numeric value is 22.
                struct proc_regionwithpathinfo regionInfo;
                if (proc_pidinfo(pid, 22, nextAddr, &regionInfo, sizeof(regionInfo)) <= 0) {
                    // The API signals the end of the region list as an error.
                    if (errno == ESRCH || errno == EINVAL)
                        break;
                    throw SysError("getting pid %1% region path", pid);
                }

                unchecked[std::string(regionInfo.prp_vip.vip_path)].emplace(fmt("{libproc/%d/region}", pid));

                nextAddr = regionInfo.prp_prinfo.pri_address + regionInfo.prp_prinfo.pri_size;
            }

            // Arguments and environment variables. Environment variables of
            // entitled binaries cannot be read unless Nix has the
            // com.apple.private.read-environment-variables entitlement or SIP
            // is disabled. Arguments remain readable, but must be ignored: a
            // path passed to `nix-store --delete` must not root itself.
            int sysctlName[3] = {CTL_KERN, KERN_PROCARGS2, pid};
            size_t argsSize = 0;
            if (sysctl(sysctlName, 3, nullptr, &argsSize, nullptr, 0) < 0)
                throw SysError("reading pid %1% arguments", pid);

            std::vector<char> args(argsSize);
            if (sysctl(sysctlName, 3, args.data(), &argsSize, nullptr, 0) < 0)
                throw SysError("reading pid %1% arguments", pid);

            if (argsSize < args.size())
                args.resize(argsSize);

            // The first four bytes contain argc, followed by the executable,
            // argc arguments, then the environment. Skip the executable and
            // arguments before searching for store paths.
            if (args.size() < sizeof(int))
                continue;

            int argc;
            std::memcpy(&argc, args.data(), sizeof(argc));
            if (argc < 0)
                continue;

            auto argsIter = args.begin() + sizeof(argc);
            auto entriesToSkip = static_cast<std::size_t>(argc) + 1;
            for (std::size_t i = 0; argsIter != args.end() && i < entriesToSkip; ++i) {
                argsIter = std::find(argsIter, args.end(), '\0');
                argsIter = std::find_if(argsIter, args.end(), [](char ch) { return ch != '\0'; });
            }

            if (argsIter != args.end()) {
                using RegexIterator = boost::regex_iterator<decltype(argsIter)>;
                for (RegexIterator i(argsIter, args.end(), storePathRegex), end; i != end; ++i)
                    unchecked[i->str()].emplace(fmt("{libproc/%d/environ}", pid));
            }

            // Per-thread working directories.
            struct proc_taskallinfo taskAllInfo;
            if (proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &taskAllInfo, sizeof(taskAllInfo)) <= 0)
                throw SysError("reading pid %1% tasks", pid);

            // If the process doesn't have the per-thread cwd flag, the
            // process-wide cwd above is sufficient.
            if (taskAllInfo.pbsd.pbi_flags & PROC_FLAG_THCWD) {
                std::vector<uint64_t> tids(taskAllInfo.ptinfo.pti_threadnum);
                auto tidBufSize =
                    proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, tids.data(), tids.size() * sizeof(uint64_t));
                if (tidBufSize <= 0)
                    throw SysError("listing pid %1% threads", pid);

                tids.resize(tidBufSize / sizeof(uint64_t));
                for (auto tid : tids) {
                    struct proc_threadwithpathinfo threadPathInfo;
                    if (proc_pidinfo(pid, PROC_PIDTHREADPATHINFO, tid, &threadPathInfo, sizeof(threadPathInfo)) <= 0)
                        throw SysError("reading pid %1% thread %2% cwd", pid, tid);

                    unchecked[std::string(threadPathInfo.pvip.vip_path)].emplace(
                        fmt("{libproc/%d/thread/%d/cwd}", pid, tid));
                }
            }
        } catch (SysError & e) {
            // Processes can exit or become inaccessible at any point while
            // they are inspected. Protected processes also reject some of
            // these calls.
            if (e.is(std::errc::no_such_file_or_directory) || e.is(std::errc::no_such_process)
                || e.is(std::errc::invalid_argument) || e.is(std::errc::permission_denied)
                || e.is(std::errc::operation_not_permitted) || e.is(std::errc::io_error))
                continue;
            throw;
        }
    }
}

} // namespace nix

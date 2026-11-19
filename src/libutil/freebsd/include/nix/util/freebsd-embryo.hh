#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"
#include "nix/util/types.hh"

#include <filesystem>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/ucred.h>

namespace nix::freebsd {

/**
 * A process created by `pdrfork(RFEMBRYO)`: it has no address space and no
 * program, and runs nothing until `exec()`. Everything it needs is installed
 * from the outside by the `pdset*` system calls, so unlike with
 * `startProcess()` none of our code ever runs in the child, and every setup
 * error is reported to the caller directly, as an exception.
 *
 * An embryo takes from its creator only its credentials, resource limits,
 * process group and scheduling parameters, plus the default umask, `CMASK`
 * (022). It has no file descriptors, no working directory and no root, blocks
 * no signals, and gives every signal its default disposition.
 *
 * It is configured builder-style: each setter acts on the process at once and
 * returns the `Embryo`, so calls can be chained, and nothing runs until the
 * separate final step, `exec()`. Destroying an `Embryo` that has not been
 * `exec()`ed closes its process descriptor, which destroys the process.
 */
class Embryo
{
    AutoCloseFD procDesc;
    pid_t pid;

public:
    /**
     * @param dieWithParent Unless false, closing the process descriptor --
     * including by our exiting -- kills the process with `SIGKILL`.
     */
    explicit Embryo(bool dieWithParent = true);

    /**
     * Install our `localFd` as the process's `remoteFd`, replacing any
     * descriptor already installed there.
     */
    Embryo & setFd(Descriptor remoteFd, Descriptor localFd);

    /**
     * Set the process's working directory to the directory `dirFd` refers to.
     */
    Embryo & chdir(Descriptor dirFd);

    /**
     * Set the process's root to the directory `dirFd` refers to, or by
     * default to our own root.
     */
    Embryo & chroot(Descriptor dirFd = AT_FDROOT);

    /**
     * Make the process the leader of a new session, as `setsid(2)`, so that
     * it has no controlling terminal and its own process group.
     */
    Embryo & setsid();

    /**
     * Set the process's credentials, as `setcred(2)`.
     */
    Embryo & setCred(u_int flags, const struct setcred & cred);

    /**
     * Set one of the process's resource limits, as `setrlimit(2)`.
     */
    Embryo & setRlimit(int resource, const struct rlimit & limit);

    /**
     * Put the process in the jail `jid`, as `jail_attach(2)`. This also sets
     * its root to the jail's.
     */
    Embryo & jailAttach(int jid);

    /**
     * Apply the `procctl(2)` request `com` to the process.
     */
    Embryo & procctl(int com, void * data);

    /**
     * Give the process the program at `program`, resolved against the root
     * and working directory it has been given, and run it.
     *
     * A failure to load the program is reported here, and destroys the
     * process.
     *
     * @return The running process. Its `Pid` keeps the process descriptor.
     */
    Pid exec(const std::filesystem::path & program, const Strings & args, const Strings & env) &&;
};

} // namespace nix::freebsd

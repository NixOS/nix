#include "nix/util/freebsd-embryo.hh"
#include "nix/util/system-error.hh"
#include "nix/util/util.hh"

#include <sys/procdesc.h>
#include <unistd.h>

/* The embryonic-process interface FreeBSD does not have yet. Each one is the
   `pd` form of an existing system call, acting on the embryo specified by the
   process descriptor `fd` rather than on the caller, like the `pdset*` calls
   that `<sys/procdesc.h>` already declares. */
extern "C" {
int pdsetsid(int fd);
int pdsetcred(int fd, u_int flags, const struct setcred * wcred, size_t size);
int pdsetrlimit(int fd, int resource, const struct rlimit * rlp);
int pdjail_attach(int fd, int jid);
}

namespace nix::freebsd {

Embryo::Embryo(bool dieWithParent)
{
    int fd;
    pid = pdrfork(&fd, PD_CLOEXEC | (dieWithParent ? 0 : PD_DAEMON), RFEMBRYO);
    if (pid == -1)
        throw SysError("creating an embryonic process");
    procDesc = fd;
}

Embryo & Embryo::setFd(Descriptor remoteFd, Descriptor localFd)
{
    if (pdsetfd(procDesc.get(), remoteFd, localFd, nullptr) == -1)
        throw SysError("installing file descriptor %d as %d in process %d", localFd, remoteFd, pid);
    return *this;
}

Embryo & Embryo::chdir(Descriptor dirFd)
{
    if (pdchdir(procDesc.get(), dirFd) == -1)
        throw SysError("setting the working directory of process %d", pid);
    return *this;
}

Embryo & Embryo::chroot(Descriptor dirFd)
{
    if (pdchroot(procDesc.get(), dirFd) == -1)
        throw SysError("setting the root directory of process %d", pid);
    return *this;
}

Embryo & Embryo::setsid()
{
    if (pdsetsid(procDesc.get()) == -1)
        throw SysError("creating a new session for process %d", pid);
    return *this;
}

Embryo & Embryo::setCred(u_int flags, const struct setcred & cred)
{
    if (pdsetcred(procDesc.get(), flags, &cred, sizeof(cred)) == -1)
        throw SysError("setting the credentials of process %d", pid);
    return *this;
}

Embryo & Embryo::setRlimit(int resource, const struct rlimit & limit)
{
    if (pdsetrlimit(procDesc.get(), resource, &limit) == -1)
        throw SysError("setting resource limit %d of process %d", resource, pid);
    return *this;
}

Embryo & Embryo::jailAttach(int jid)
{
    if (pdjail_attach(procDesc.get(), jid) == -1)
        throw SysError("attaching process %d to jail %d", pid, jid);
    return *this;
}

Embryo & Embryo::procctl(int com, void * data)
{
    if (pdprocctl(procDesc.get(), com, data) == -1)
        throw SysError("applying procctl request %d to process %d", com, pid);
    return *this;
}

Pid Embryo::exec(const std::filesystem::path & program, const Strings & args, const Strings & env) &&
{
    if (pdexec(
            procDesc.get(), AT_FDCWD, program.c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(env).data(), 0)
        == -1)
        throw SysError("executing %s", PathFmt(program));
    return Pid(pid, std::move(procDesc));
}

} // namespace nix::freebsd

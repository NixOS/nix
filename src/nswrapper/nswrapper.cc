#include "nix/util/file-descriptor.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"
#include "nix/util/util.hh"
#include <grp.h>
#include <string>
#include <sys/mount.h>
#include <sys/types.h>

namespace nix {

static uid_t parseUid(std::string_view value)
{
    auto parsed = string2Int<uid_t>(value);
    if (!parsed.has_value())
        throw UsageError("Not a valid integer");
    return parsed.value();
}

void mainWrapped(int argc, char ** argv)
{
    if (argc < 4)
        throw UsageError("Usage: %1% first_uid num_uids command [args...]", argv[0]);

    auto baseExtra = parseUid(argv[1]);
    auto numExtra = parseUid(argv[2]);

    auto currentUid = geteuid();
    auto currentGid = getegid();

    if (numExtra == 0)
        throw UsageError("Must have at least 1 extra UID");

    if (baseExtra >= baseExtra + numExtra)
        throw UsageError("Extra UIDs must not wrap");

    if (baseExtra <= currentUid && currentUid < baseExtra + numExtra)
        throw UsageError("Extra UIDs must not include current UID");

    if (baseExtra <= currentGid && currentGid < baseExtra + numExtra)
        throw UsageError("Extra GIDs must not include current GID");

    // Unfortunately we can't call `newuidmap` on ourselves.
    // We have to be in a new user namespace or `newuidmap`
    // will refuse to add new users, but it won't work after
    // entering a new namespace.
    // Therefore, make a new process to call newuidmap/newgidmap from,
    // then call unshare on the parent

    auto parentPid = getpid();
    if (parentPid < 0)
        throw SysError("getpid on parent");

    Pipe toHelper;
    toHelper.create();

    Pid helper = startProcess([&]() {
        toHelper.writeSide.close();
        // Wait for the host to unshare,
        readLine(toHelper.readSide.get());

        runProgram(
            "newuidmap",
            true,
            toOsStrings({
                std::to_string(parentPid),
                // UID 0 in namespace is euid of parent
                "0",
                std::to_string(currentUid),
                "1",
                // numExtra starting from baseExtra are mapped 1:1 to outside namespace
                std::to_string(baseExtra),
                std::to_string(baseExtra),
                std::to_string(numExtra),
            }));

        runProgram(
            "newgidmap",
            true,
            toOsStrings({
                std::to_string(parentPid),
                // GID 0 in namespace is egid of parent
                "0",
                std::to_string(currentGid),
                "1",
                // numExtra starting from baseExtra are mapped 1:1 to outside namespace
                std::to_string(baseExtra),
                std::to_string(baseExtra),
                std::to_string(numExtra),
            }));

        exit(0);
    });

    toHelper.readSide.close();

    // CLONE_NEWUSER (user namespace) so we can remap users
    // CLONE_NEWNS (mount namespace) so we can remount devpts
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) < 0)
        throw SysError("creating new namespace");

    writeFull(toHelper.writeSide.get(), "0\n");

    if (!statusOk(helper.wait()))
        throw Error("adding uids/gids to namespace");

    if (setresuid(0, 0, 0) < 0)
        throw SysError("setting uid");

    if (setresgid(0, 0, 0) < 0)
        throw SysError("setting gid");

    if (setgroups(0, nullptr) < 0)
        throw SysError("dropping groups");

    // We have to mount a new devpts, otherwise we won't be able to chown ptses for build users
    if (mount("none", "/dev/pts", "devpts", 0, "mode=0620") < 0)
        throw SysError("mounting /dev/pts");

    if (execvp(argv[3], argv + 3) < 0)
        throw SysError("executing wrapped program");
}

} // namespace nix

int main(int argc, char ** argv)
{
    return nix::handleExceptions(argv[0], [&]() { nix::mainWrapped(argc, argv); });
}

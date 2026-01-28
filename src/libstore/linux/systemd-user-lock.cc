#include <future>
#include <sys/socket.h>

#include "nix/store/user-lock.hh"
#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/util/serialise.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/logging.hh"

namespace nix {

struct SystemdUserLock : UserLock
{
    Pid pid;
    AutoCloseFD fdNamespace;
    uid_t firstUid = 0;
    gid_t firstGid = 0;
    uid_t nrIds = 1;

    uid_t getUID() override
    {
        assert(firstUid);
        return firstUid;
    }

    gid_t getUIDCount() override
    {
        return nrIds;
    }

    gid_t getGID() override
    {
        assert(firstGid);
        return firstGid;
    }

    std::vector<gid_t> getSupplementaryGIDs() override
    {
        return {};
    }

    static std::unique_ptr<UserLock> acquire(uid_t nrIds)
    {
        experimentalFeatureSettings.require(Xp::AutoAllocateUids);

        Pid pid = startProcess(
            [&]() {
                // Keep process waiting forever, we want to keep it around so we can read uid_map
                std::promise<void>().get_future().wait();
            },
            {.cloneFlags = CLONE_NEWUSER});

        auto pid_path = std::filesystem::path("/proc") / std::to_string(pid);

        // We should still count as the namespace owner from here, getting the namespace fd
        // from outside the process is a bit easier than sending it from inside.
        AutoCloseFD fdNamespace = open((pid_path / "ns" / "user").c_str(), O_RDONLY | O_CLOEXEC);
        if (!fdNamespace)
            throw SysError("opening namespace file descriptor");

        auto varlinkSocket = createUnixDomainSocket();
        nix::connect(varlinkSocket.get(), "/run/systemd/io.systemd.NamespaceResource");

        nlohmann::json query = {
            {"method", "io.systemd.NamespaceResource.AllocateUserRange"},
            {
                "parameters",
                {{"name", ""}, {"mangleName", true}, {"size", nrIds}, {"userNamespaceFileDescriptor", 0}},
            }};

        std::string query_string = query.dump();

        // We have to manually use sendmsg here, none of the convenient nix functions can do SCM_RIGHTS

        // C strings are always null terminated. We want our query to be null terminated anyway,
        // use the C string null terminator
        struct iovec iov{.iov_base = (void *) query_string.c_str(), .iov_len = query_string.length() + 1};

        // This is going to be cast to a struct cmsghdr, but we're responsible for its alignment
        alignas(struct cmsghdr) uint8_t controlData[CMSG_SPACE(sizeof(int))];

        struct msghdr request{
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = &controlData,
            .msg_controllen = sizeof(controlData),
        };

        auto cmsg = CMSG_FIRSTHDR(&request);
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        *((int *) CMSG_DATA(cmsg)) = dup(fdNamespace.get());

        if (sendmsg(varlinkSocket.get(), &request, 0) < 0)
            throw SysError("sending message to systemd-nsresourced");

        FdSource varlinkSource(varlinkSocket.get());
        auto response = nlohmann::json::parse(varlinkSource.readLine(true, '\0'));

        if (response.contains("error"))
            throw Error("'%1%' returned by systemd-nsresourced", response["error"]);

        auto uidMapContent = readFile(pid_path / "uid_map");

        std::optional<uid_t> uid = std::nullopt;

        for (auto line : splitString<std::vector<std::string>>(uidMapContent, "\n")) {
            auto parts = tokenizeString<std::vector<std::string>>(line);
            if (parts.size() != 3)
                throw Error("Kernel returned invalid uid map");
            auto internalUid = string2Int<uid_t>(parts[0]);
            auto externalUid = string2Int<uid_t>(parts[1]);
            if (internalUid.has_value() && internalUid == 0) {
                uid = externalUid;
                break;
            }
        }

        if (!uid.has_value())
            throw Error("systemd-nsresourced did not assign uid");

        auto lock = std::make_unique<SystemdUserLock>();
        lock->fdNamespace = std::move(fdNamespace);
        lock->pid = std::move(pid);
        lock->nrIds = nrIds;
        // systemd-nsresourced always assigns the same value for uid and gid
        lock->firstUid = uid.value();
        lock->firstGid = uid.value();

        return lock;
    }
};

std::unique_ptr<UserLock> linux::acquireSystemdUserLock(uid_t nrIds)
{
    return SystemdUserLock::acquire(nrIds);
}

} // namespace nix

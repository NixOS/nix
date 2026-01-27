#include <future>

#include "nix/store/user-lock.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/util/serialise.hh"
#include "nix/util/unix-domain-socket.hh"

namespace nix {

struct SystemdUserLock : UserLock
{
    static constexpr gid_t defaultInternalUid = 1000;

    Pid pid;
    AutoCloseFD fdNamespace;
    uid_t firstUid = 0;
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
        // systemd-nsresourced always assigns the same value for UIDs and GIDs
        return firstUid;
    }

    std::vector<gid_t> getSupplementaryGIDs() override
    {
        return {};
    }

    uid_t getSandboxedUID() override
    {
        return (nrIds == 1) ? defaultInternalUid : 0;
    }

    uid_t getSandboxedGID() override
    {
        // systemd-nsresourced always assigns the same value for UIDs and GIDs
        return getSandboxedUID();
    }

    std::optional<AutoCloseFD> openUserNamespace() override
    {
        auto fd = AutoCloseFD(dup(fdNamespace.get()));
        if (!fd)
            throw SysError("duplicating user namespace file descriptor");
        return fd;
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

        uid_t internalUid = nrIds == 1 ? defaultInternalUid : 0;

        nlohmann::json query = {
            {"method", "io.systemd.NamespaceResource.AllocateUserRange"},
            {
                "parameters",
                {
                    {"name", ""},
                    {"mangleName", true},
                    {"size", nrIds},
                    {"target", internalUid},
                    {"userNamespaceFileDescriptor", 0},
                },
            },
        };

        std::string query_string = query.dump();

        // Varlink protocol requires null-terminated messages
        // Include the null terminator in the data we send
        std::string_view data(query_string.c_str(), query_string.length() + 1);
        assert(!data.empty() && data[data.size() - 1] == '\0');

        std::array<int, 1> fds = {fdNamespace.get()};
        unix::sendMessageWithFds(varlinkSocket.get(), data, fds);

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
            auto lineInternalUid = string2Int<uid_t>(parts[0]);
            auto lineExternalUid = string2Int<uid_t>(parts[1]);
            if (lineInternalUid.has_value() && lineInternalUid == internalUid) {
                uid = lineExternalUid;
                break;
            }
        }

        if (!uid.has_value())
            throw Error("systemd-nsresourced did not assign uid");

        auto lock = std::make_unique<SystemdUserLock>();
        lock->fdNamespace = std::move(fdNamespace);
        lock->pid = std::move(pid);
        lock->nrIds = nrIds;
        lock->firstUid = uid.value();

        return lock;
    }
};

std::unique_ptr<UserLock> linux::acquireSystemdUserLock(uid_t nrIds)
{
    return SystemdUserLock::acquire(nrIds);
}

} // namespace nix

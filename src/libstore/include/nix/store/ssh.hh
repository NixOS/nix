#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/util/sync.hh"
#include "nix/util/url.hh"
#include "nix/util/processes.hh"
#include "nix/util/file-system.hh"

namespace nix {

Strings getNixSshOpts();

class SSHMaster
{
private:

    ParsedURL::Authority authority;
    std::string hostnameAndUser;
    bool fakeSSH;
    const std::string keyFile;
    /**
     * Raw bytes, not Base64 encoding.
     */
    const std::string sshPublicHostKey;
    const bool useMaster;
    const bool compress;
    const Descriptor logFD;

    const ref<const AutoDelete> tmpDir;

    struct State
    {
#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
        Pid sshMaster;
#endif
        Path socketPath;
    };

    Sync<State> state_;

    void addCommonSSHOpts(Strings & args);
    bool isMasterRunning();

#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
    Path startMaster();
#endif

public:

    SSHMaster(
        const ParsedURL::Authority & authority,
        std::string_view keyFile,
        std::string_view sshPublicHostKey,
        bool useMaster,
        bool compress,
        Descriptor logFD = INVALID_DESCRIPTOR);

    struct Connection
    {
#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
        Pid sshPid;
#endif
        AutoCloseFD out, in;

        /**
         * Try to set the buffer size in both directions to the
         * designated amount, if possible. If not possible, does
         * nothing.
         *
         * Current implementation is to use `fcntl` with `F_SETPIPE_SZ`,
         * which is Linux-only. For this implementation, `size` must
         * convertible to an `int`. In other words, it must be within
         * `[0, INT_MAX]`.
         */
        void trySetBufferSize(size_t size);
    };

    /**
     * @param command The command (arg vector) to execute.
     *
     * @param extraSshArgs Extra arguments to pass to SSH (not the command to
     * execute). Will not be used when "fake SSHing" to the local
     * machine.
     */
    std::unique_ptr<Connection> startCommand(Strings && command, Strings && extraSshArgs = {});
};

} // namespace nix

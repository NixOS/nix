#pragma once

#include "config.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "gc-store.hh"

namespace nix {

typedef enum { smEnabled, smRelaxed, smDisabled } SandboxMode;

struct LocalStoreConfig : virtual LocalFSStoreConfig
{
    LocalStoreConfig(const Params &);

    const std::string name() override { return "Local Store"; }

    Setting<SandboxMode> sandboxMode{
        (StoreConfig*) this,
        #if __linux__
          smEnabled
        #else
          smDisabled
        #endif
        , "sandbox",
        R"(
          If set to `true`, builds will be performed in a *sandboxed
          environment*, i.e., they’re isolated from the normal file system
          hierarchy and will only see their dependencies in the Nix store,
          the temporary build directory, private versions of `/proc`,
          `/dev`, `/dev/shm` and `/dev/pts` (on Linux), and the paths
          configured with the `sandbox-paths` option. This is useful to
          prevent undeclared dependencies on files in directories such as
          `/usr/bin`. In addition, on Linux, builds run in private PID,
          mount, network, IPC and UTS namespaces to isolate them from other
          processes in the system (except that fixed-output derivations do
          not run in private network namespace to ensure they can access the
          network).

          Currently, sandboxing only work on Linux and macOS. The use of a
          sandbox requires that Nix is run as root (so you should use the
          “build users” feature to perform the actual builds under different
          users than root).

          If this option is set to `relaxed`, then fixed-output derivations
          and derivations that have the `__noChroot` attribute set to `true`
          do not run in sandboxes.

          The default is `true` on Linux and `false` on all other platforms.
        )",
        {"build-use-chroot", "build-use-sandbox"}};

    Setting<PathSet> sandboxPaths;

    Setting<bool> sandboxFallback{this, true, "sandbox-fallback",
        "Whether to disable sandboxing when the kernel doesn't allow it."};

#if __linux__
    Setting<std::string> sandboxShmSize{
        (StoreConfig*) this, "50%", "sandbox-dev-shm-size",
        R"(
          This option determines the maximum size of the `tmpfs` filesystem
          mounted on `/dev/shm` in Linux sandboxes. For the format, see the
          description of the `size` option of `tmpfs` in mount8. The default
          is `50%`.
        )"};

    Setting<Path> sandboxBuildDir{this, "/build", "sandbox-build-dir",
        "The build directory inside the sandbox."};
#endif

    Setting<PathSet> allowedImpureHostPrefixes{this, {}, "allowed-impure-host-deps",
        "Which prefixes to allow derivations to ask for access to (primarily for Darwin)."};

#if __APPLE__
    Setting<bool> darwinLogSandboxViolations{this, false, "darwin-log-sandbox-violations",
        "Whether to log Darwin sandbox access violations to the system log."};
#endif

    Setting<bool> runDiffHook{
        (StoreConfig*) this, false, "run-diff-hook",
        R"(
          If true, enable the execution of the `diff-hook` program.

          When using the Nix daemon, `run-diff-hook` must be set in the
          `nix.conf` configuration file, and cannot be passed at the command
          line.
        )"};

    PathSetting diffHook{
        (StoreConfig*) this, true, "", "diff-hook",
        R"(
          Absolute path to an executable capable of diffing build
          results. The hook is executed if `run-diff-hook` is true, and the
          output of a build is known to not be the same. This program is not
          executed to determine if two results are the same.

          The diff hook is executed by the same user and group who ran the
          build. However, the diff hook does not have write access to the
          store path just built.

          The diff hook program receives three parameters:

          1.  A path to the previous build's results

          2.  A path to the current build's results

          3.  The path to the build's derivation

          4.  The path to the build's scratch directory. This directory will
              exist only if the build was run with `--keep-failed`.

          The stderr and stdout output from the diff hook will not be
          displayed to the user. Instead, it will print to the nix-daemon's
          log.

          When using the Nix daemon, `diff-hook` must be set in the `nix.conf`
          configuration file, and cannot be passed at the command line.
        )"};

    Setting<bool> requireSigs{
        (StoreConfig*) this, true, "require-sigs",
        R"(
          If set to `true` (the default), any non-content-addressed path added
          or copied to the Nix store (e.g. when substituting from a binary
          cache) must have a signature by a trusted key. A trusted key is one
          listed in `trusted-public-keys`, or a public key counterpart to a
          private key stored in a file listed in `secret-key-files`.

          Set to `false` to disable signature checking and trust all
          non-content-addressed paths unconditionally.

          (Content-addressed paths are inherently trustworthy and thus
          unaffected by this configuration option.)
        )"};

    Setting<std::string> preBuildHook{
        (StoreConfig*) this, "", "pre-build-hook",
        R"(
          If set, the path to a program that can set extra derivation-specific
          settings for this system. This is used for settings that can't be
          captured by the derivation model itself and are too variable between
          different versions of the same system to be hard-coded into nix.

          The hook is passed the derivation path and, if sandboxes are
          enabled, the sandbox directory. It can then modify the sandbox and
          send a series of commands to modify various settings to stdout. The
          currently recognized commands are:

            - `extra-sandbox-paths`\
              Pass a list of files and directories to be included in the
              sandbox for this build. One entry per line, terminated by an
              empty line. Entries have the same format as `sandbox-paths`.
        )"};

#if __linux__
    Setting<bool> filterSyscalls{
        (StoreConfig*) this, true, "filter-syscalls",
        R"(
          Whether to prevent certain dangerous system calls, such as
          creation of setuid/setgid files or adding ACLs or extended
          attributes. Only disable this if you're aware of the
          security implications.
        )"};

    Setting<bool> allowNewPrivileges{
        (StoreConfig*) this, false, "allow-new-privileges",
        R"(
          (Linux-specific.) By default, builders on Linux cannot acquire new
          privileges by calling setuid/setgid programs or programs that have
          file capabilities. For example, programs such as `sudo` or `ping`
          will fail. (Note that in sandbox builds, no such programs are
          available unless you bind-mount them into the sandbox via the
          `sandbox-paths` option.) You can allow the use of such programs by
          enabling this option. This is impure and usually undesirable, but
          may be useful in certain scenarios (e.g. to spin up containers or
          set up userspace network interfaces in tests).
        )"};
#endif

    Setting<uint64_t> minFree{
        (StoreConfig*) this, 0, "min-free",
        R"(
          When free disk space in `/nix/store` drops below `min-free` during a
          build, Nix performs a garbage-collection until `max-free` bytes are
          available or there is no more garbage. A value of `0` (the default)
          disables this feature.
        )"};

    Setting<uint64_t> maxFree{
        (StoreConfig*) this, std::numeric_limits<uint64_t>::max(), "max-free",
        R"(
          When a garbage collection is triggered by the `min-free` option, it
          stops as soon as `max-free` bytes are available. The default is
          infinity (i.e. delete all garbage).
        )"};

    Setting<uint64_t> minFreeCheckInterval{
        (StoreConfig*) this, 5, "min-free-check-interval",
        "Number of seconds between checking free disk space."};

    Setting<size_t> narBufferSize{
        (StoreConfig*) this, 32 * 1024 * 1024, "nar-buffer-size",
        "Maximum size of NARs before spilling them to disk."};

    Setting<bool> allowSymlinkedStore;
        ;
};

}

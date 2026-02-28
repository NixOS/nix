#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/configuration.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/users.hh"
#include "nix/store/build/derivation-builder.hh"

#include "nix/store/config.hh"

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace nix {

typedef enum { smEnabled, smRelaxed, smDisabled } SandboxMode;

template<>
SandboxMode BaseSetting<SandboxMode>::parse(const std::string & str) const;
template<>
std::string BaseSetting<SandboxMode>::to_string() const;

template<>
PathsInChroot BaseSetting<PathsInChroot>::parse(const std::string & str) const;
template<>
std::string BaseSetting<PathsInChroot>::to_string() const;

template<>
struct BaseSetting<PathsInChroot>::trait
{
    static constexpr bool appendable = true;
};

template<>
void BaseSetting<PathsInChroot>::appendOrSet(PathsInChroot newValue, bool append);

struct GCSettings : public virtual Config
{
    Setting<off_t> reservedSize{
        this,
        8 * 1024 * 1024,
        "gc-reserved-space",
        "Amount of reserved disk space for the garbage collector.",
    };

    Setting<bool> keepOutputs{
        this,
        false,
        "keep-outputs",
        R"(
          If `true`, the garbage collector keeps the outputs of
          non-garbage derivations. If `false` (default), outputs are
          deleted unless they are GC roots themselves (or reachable from other
          roots).

          In general, outputs must be registered as roots separately. However,
          even if the output of a derivation is registered as a root, the
          collector still deletes store paths that are used only at build
          time (e.g., the C compiler, or source tarballs downloaded from the
          network). To prevent it from doing so, set this option to `true`.
        )",
        {"gc-keep-outputs"},
    };

    Setting<bool> keepDerivations{
        this,
        true,
        "keep-derivations",
        R"(
          If `true` (default), the garbage collector keeps the derivations
          from which non-garbage store paths were built. If `false`, they are
          deleted unless explicitly registered as a root (or reachable from
          other roots).

          Keeping derivation around is useful for querying and traceability
          (e.g., it allows you to ask with what dependencies or options a
          store path was built), so by default this option is on. Turn it off
          to save a bit of disk space (or a lot if `keep-outputs` is also
          turned on).
        )",
        {"gc-keep-derivations"},
    };

    Setting<uint64_t> minFree{
        this,
        0,
        "min-free",
        R"(
          When free disk space in `/nix/store` drops below `min-free` during a
          build, Nix performs a garbage-collection until `max-free` bytes are
          available or there is no more garbage. A value of `0` (the default)
          disables this feature.
        )",
    };

    // n.b. this is deliberately int64 max rather than uint64 max because
    // this goes through the Nix language JSON parser and thus needs to be
    // representable in Nix language integers.
    Setting<uint64_t> maxFree{
        this,
        std::numeric_limits<int64_t>::max(),
        "max-free",
        R"(
          When a garbage collection is triggered by the `min-free` option, it
          stops as soon as `max-free` bytes are available. The default is
          infinity (i.e. delete all garbage).
        )",
    };

    Setting<uint64_t> minFreeCheckInterval{
        this,
        5,
        "min-free-check-interval",
        "Number of seconds between checking free disk space.",
    };
};

const uint32_t maxIdsPerBuild =
#ifdef __linux__
    1 << 16
#else
    1
#endif
    ;

struct AutoAllocateUidSettings : public virtual Config
{
    Setting<uint32_t> startId{
        this,
#ifdef __linux__
        0x34000000,
#else
        56930,
#endif
        "start-id",
        "The first UID and GID to use for dynamic ID allocation."};

    Setting<uint32_t> uidCount{
        this,
#ifdef __linux__
        maxIdsPerBuild * 128,
#else
        128,
#endif
        "id-count",
        "The number of UIDs/GIDs to use for dynamic ID allocation."};
};

/**
 * Either about local store or local building
 *
 * These are things that should not be part of the global settings, but
 * should be per-local-store at a minimum. We expose them from
 * `settings` with `settings.getLocalSettings()` for now, but we also
 * have `localStore.config->getLocalSettings()` as a way to get them
 * too. Even though both ways will actually draw from the same global
 * variable, we would much prefer if you use the second one, because
 * this will prepare the code base to making these *actual*, rather than
 * pretend, per-store settings.
 */
struct LocalSettings : public virtual Config, public GCSettings, public AutoAllocateUidSettings
{
    /**
     * Get the GC settings.
     */
    GCSettings & getGCSettings()
    {
        return *this;
    }

    const GCSettings & getGCSettings() const
    {
        return *this;
    }

    /**
     * Get AutoAllocateUidSettings if auto-allocate-uids is enabled.
     * @return Pointer to settings if enabled, nullptr otherwise.
     */
    const AutoAllocateUidSettings * getAutoAllocateUidSettings() const
    {
        return autoAllocateUids ? this : nullptr;
    }

    Setting<unsigned int> buildCores{
        this,
        0,
        "cores",
        R"(
          Sets the value of the `NIX_BUILD_CORES` environment variable in the [invocation of the `builder` executable](@docroot@/store/building.md#builder-execution) of a derivation.
          The `builder` executable can use this variable to control its own maximum amount of parallelism.

          <!--
          FIXME(@fricklerhandwerk): I don't think this should even be mentioned here.
          A very generic example using `derivation` and `xargs` may be more appropriate to explain the mechanism.
          Using `mkDerivation` as an example requires being aware of that there are multiple independent layers that are completely opaque here.
          -->
          For instance, in Nixpkgs, if the attribute `enableParallelBuilding` for the `mkDerivation` build helper is set to `true`, it passes the `-j${NIX_BUILD_CORES}` flag to GNU Make.

          If set to `0`, nix will detect the number of CPU cores and pass this number via `NIX_BUILD_CORES`.

          > **Note**
          >
          > The number of parallel local Nix build jobs is independently controlled with the [`max-jobs`](#conf-max-jobs) setting.
        )",
        {"build-cores"}};

    Setting<bool> fsyncMetadata{
        this,
        true,
        "fsync-metadata",
        R"(
          If set to `true`, changes to the Nix store metadata (in
          `/nix/var/nix/db`) are synchronously flushed to disk. This improves
          robustness in case of system crashes, but reduces performance. The
          default is `true`.
        )"};

    Setting<bool> fsyncStorePaths{
        this,
        false,
        "fsync-store-paths",
        R"(
          Whether to call `fsync()` on store paths before registering them, to
          flush them to disk. This improves robustness in case of system crashes,
          but reduces performance. The default is `false`.
        )"};

#ifndef _WIN32
    // FIXME: remove this option, `fsync-store-paths` is faster.
    Setting<bool> syncBeforeRegistering{
        this, false, "sync-before-registering", "Whether to call `sync()` before registering a path as valid."};
#endif

    Setting<bool> autoOptimiseStore{
        this,
        false,
        "auto-optimise-store",
        R"(
          If set to `true`, Nix automatically detects files in the store
          that have identical contents, and replaces them with hard links to
          a single copy. This saves disk space. If set to `false` (the
          default), you can still run `nix-store --optimise` to get rid of
          duplicate files.
        )"};

    Setting<size_t> narBufferSize{
        this, 32 * 1024 * 1024, "nar-buffer-size", "Maximum size of NARs before spilling them to disk."};

    Setting<bool> allowSymlinkedStore{
        this,
        false,
        "allow-symlinked-store",
        R"(
          If set to `true`, Nix stops complaining if the store directory
          (typically `/nix/store`) contains symlink components.

          This risks making some builds "impure" because builders sometimes
          "canonicalise" paths by resolving all symlink components. Problems
          occur if those builds are then deployed to machines where /nix/store
          resolves to a different location from that of the build machine. You
          can enable this setting if you are sure you're not going to do that.
        )"};

    Setting<std::string> buildUsersGroup{
        this,
        "",
        "build-users-group",
        R"(
          This options specifies the Unix group containing the Nix build user
          accounts. In multi-user Nix installations, builds should not be
          performed by the Nix account since that would allow users to
          arbitrarily modify the Nix store and database by supplying specially
          crafted builders; and they cannot be performed by the calling user
          since that would allow him/her to influence the build result.

          Therefore, if this option is non-empty and specifies a valid group,
          builds are performed under the user accounts that are a member
          of the group specified here (as listed in `/etc/group`). Those user
          accounts should not be used for any other purpose\!

          Nix never runs two builds under the same user account at the
          same time. This is to prevent an obvious security hole: a malicious
          user writing a Nix expression that modifies the build result of a
          legitimate Nix expression being built by another user. Therefore it
          is good to have as many Nix build user accounts as you can spare.
          (Remember: uids are cheap.)

          The build users should have permission to create files in the Nix
          store, but not delete them. Therefore, `/nix/store` should be owned
          by the Nix account, its group should be the group specified here,
          and its mode should be `1775`.

          If the build users group is empty, builds are performed under
          the uid of the Nix process (that is, the uid of the caller if
          `NIX_REMOTE` is empty, the uid under which the Nix daemon runs if
          `NIX_REMOTE` is `daemon`). Obviously, this should not be used
          with a nix daemon accessible to untrusted clients.

          Defaults to `nixbld` when running as root, *empty* otherwise.
        )",
        {},
        false};

    Setting<bool> autoAllocateUids{
        this,
        false,
        "auto-allocate-uids",
        R"(
          Whether to select UIDs for builds automatically, instead of using the
          users in `build-users-group`.

          UIDs are allocated starting at 872415232 (0x34000000) on Linux and 56930 on macOS.
        )",
        {},
        true,
        Xp::AutoAllocateUids};

#ifdef __linux__
    Setting<bool> useCgroups{
        this,
        false,
        "use-cgroups",
        R"(
          Whether to execute builds inside cgroups.
          This is only supported on Linux.

          Cgroups are required and enabled automatically for derivations
          that require the `uid-range` system feature.
        )"};
#endif

    Setting<bool> impersonateLinux26{
        this,
        false,
        "impersonate-linux-26",
        "Whether to impersonate a Linux 2.6 machine on newer kernels.",
        {"build-impersonate-linux-26"}};

    Setting<SandboxMode> sandboxMode{
        this,
#ifdef __linux__
        smEnabled
#else
        smDisabled
#endif
        ,
        "sandbox",
        R"(
          If set to `true`, builds are performed in a *sandboxed
          environment*, i.e., they're isolated from the normal file system
          hierarchy and only see their dependencies in the Nix store,
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
          "build users" feature to perform the actual builds under different
          users than root).

          If this option is set to `relaxed`, then fixed-output derivations
          and derivations that have the `__noChroot` attribute set to `true`
          do not run in sandboxes.

          The default is `true` on Linux and `false` on all other platforms.
        )",
        {"build-use-chroot", "build-use-sandbox"}};

    Setting<PathsInChroot> sandboxPaths{
        this,
        {},
        "sandbox-paths",
        R"(
          A list of paths bind-mounted into Nix sandbox environments. You can
          use the syntax `target=source` to mount a path in a different
          location in the sandbox; for instance, `/bin=/nix-bin` mounts
          the path `/nix-bin` as `/bin` inside the sandbox. If *source* is
          followed by `?`, then it is not an error if *source* does not exist;
          for example, `/dev/nvidiactl?` specifies that `/dev/nvidiactl`
          only be mounted in the sandbox if it exists in the host filesystem.

          If the source is in the Nix store, then its closure is added to
          the sandbox as well.

          Depending on how Nix was built, the default value for this option
          may be empty or provide `/bin/sh` as a bind-mount of `bash`.
        )",
        {"build-chroot-dirs", "build-sandbox-paths"}};

    Setting<bool> sandboxFallback{
        this, true, "sandbox-fallback", "Whether to disable sandboxing when the kernel doesn't allow it."};

#ifndef _WIN32
    Setting<bool> requireDropSupplementaryGroups{
        this,
        isRootUser(),
        "require-drop-supplementary-groups",
        R"(
          Following the principle of least privilege,
          Nix attempts to drop supplementary groups when building with sandboxing.

          However this can fail under some circumstances.
          For example, if the user lacks the `CAP_SETGID` capability.
          Search `setgroups(2)` for `EPERM` to find more detailed information on this.

          If you encounter such a failure, setting this option to `false` enables you to ignore it and continue.
          But before doing so, you should consider the security implications carefully.
          Not dropping supplementary groups means the build sandbox is less restricted than intended.

          This option defaults to `true` when the user is root
          (since `root` usually has permissions to call setgroups)
          and `false` otherwise.
        )"};
#endif

#ifdef __linux__
    Setting<std::string> sandboxShmSize{
        this,
        "50%",
        "sandbox-dev-shm-size",
        R"(
            *Linux only*

            This option determines the maximum size of the `tmpfs` filesystem
            mounted on `/dev/shm` in Linux sandboxes. For the format, see the
            description of the `size` option of `tmpfs` in mount(8). The default
            is `50%`.
        )"};
#endif

#if defined(__linux__) || defined(__FreeBSD__)
    Setting<AbsolutePath> sandboxBuildDir{
        this,
        "/build",
        "sandbox-build-dir",
        R"(
            *Linux only*

            The build directory inside the sandbox.

            This directory is backed by [`build-dir`](#conf-build-dir) on the host.
        )"};
#endif

    Setting<std::optional<AbsolutePath>> buildDir{
        this,
        std::nullopt,
        "build-dir",
        R"(
            Override the `build-dir` store setting for all stores that have this setting.

            See also the per-store [`build-dir`](@docroot@/store/types/local-store.md#store-local-store-build-dir) setting.
        )"};

    Setting<std::set<std::filesystem::path>> allowedImpureHostPrefixes{
        this,
        {},
        "allowed-impure-host-deps",
        "Which prefixes to allow derivations to ask for access to (primarily for Darwin)."};

#ifdef __APPLE__
    Setting<bool> darwinLogSandboxViolations{
        this,
        false,
        "darwin-log-sandbox-violations",
        "Whether to log Darwin sandbox access violations to the system log."};
#endif

    Setting<bool> runDiffHook{
        this,
        false,
        "run-diff-hook",
        R"(
          If true, enable the execution of the `diff-hook` program.

          When using the Nix daemon, `run-diff-hook` must be set in the
          `nix.conf` configuration file, and cannot be passed at the command
          line.
        )"};

private:

    Setting<std::optional<AbsolutePath>> diffHook{
        this,
        std::nullopt,
        "diff-hook",
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

          4.  The path to the build's scratch directory. This directory
              exists only if the build was run with `--keep-failed`.

          The stderr and stdout output from the diff hook isn't displayed
          to the user. Instead, it prints to the nix-daemon's log.

          When using the Nix daemon, `diff-hook` must be set in the `nix.conf`
          configuration file, and cannot be passed at the command line.
        )"};

public:

    /**
     * Get the diff hook path if run-diff-hook is enabled.
     * @return Pointer to path if enabled, nullptr otherwise.
     */
    const AbsolutePath * getDiffHook() const
    {
        if (!runDiffHook.get()) {
            return nullptr;
        }
        return get(diffHook.get());
    }

    Setting<std::string> preBuildHook{
        this,
        "",
        "pre-build-hook",
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

#ifdef __linux__
    Setting<bool> filterSyscalls{
        this,
        true,
        "filter-syscalls",
        R"(
          Whether to prevent certain dangerous system calls, such as
          creation of setuid/setgid files or adding ACLs or extended
          attributes. Only disable this if you're aware of the
          security implications.
        )"};

    Setting<bool> allowNewPrivileges{
        this,
        false,
        "allow-new-privileges",
        R"(
          (Linux-specific.) By default, builders on Linux cannot acquire new
          privileges by calling setuid/setgid programs or programs that have
          file capabilities. For example, programs such as `sudo` or `ping`
          should fail. (Note that in sandbox builds, no such programs are
          available unless you bind-mount them into the sandbox via the
          `sandbox-paths` option.) You can allow the use of such programs by
          enabling this option. This is impure and usually undesirable, but
          may be useful in certain scenarios (e.g. to spin up containers or
          set up userspace network interfaces in tests).
        )"};
#endif

#if NIX_SUPPORT_ACL
    Setting<StringSet> ignoredAcls{
        this,
        {"security.selinux", "system.nfs4_acl", "security.csm"},
        "ignored-acls",
        R"(
          A list of ACLs that should be ignored, normally Nix attempts to
          remove all ACLs from files and directories in the Nix store, but
          some ACLs like `security.selinux` or `system.nfs4_acl` can't be
          removed even by root. Therefore it's best to just ignore them.
        )"};
#endif

    Setting<StringMap> impureEnv{
        this,
        {},
        "impure-env",
        R"(
          A list of items, each in the format of:

          - `name=value`: Set environment variable `name` to `value`.

          If the user is trusted (see `trusted-users` option), when building
          a fixed-output derivation, environment variables set in this option
          is passed to the builder if they are listed in [`impureEnvVars`](@docroot@/language/advanced-attributes.md#adv-attr-impureEnvVars).

          This option is useful for, e.g., setting `https_proxy` for
          fixed-output derivations and in a multi-user Nix installation, or
          setting private access tokens when fetching a private repository.
        )",
        {},   // aliases
        true, // document default
        Xp::ConfigurableImpureEnv};

    Setting<Strings> hashedMirrors{
        this,
        {},
        "hashed-mirrors",
        R"(
          A list of web servers used by `builtins.fetchurl` to obtain files by
          hash. Given a hash algorithm *ha* and a base-16 hash *h*, Nix tries to
          download the file from *hashed-mirror*/*ha*/*h*. This allows files to
          be downloaded even if they have disappeared from their original URI.
          For example, given an example mirror `http://tarballs.nixos.org/`,
          when building the derivation

          ```nix
          builtins.fetchurl {
            url = "https://example.org/foo-1.2.3.tar.xz";
            sha256 = "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae";
          }
          ```

          Nix will attempt to download this file from
          `http://tarballs.nixos.org/sha256/2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae`
          first. If it is not available there, it tries the original URI.
        )"};

    using ExternalBuilders = std::vector<ExternalBuilder>;

    Setting<ExternalBuilders> externalBuilders{
        this,
        {},
        "external-builders",
        R"(
          Helper programs that execute derivations.

          The program is passed a JSON document that describes the build environment as the final argument.
          The JSON document looks like this:

            {
              "args": [
                "-e",
                "/nix/store/vj1c3wf9…-source-stdenv.sh",
                "/nix/store/shkw4qm9…-default-builder.sh"
              ],
              "builder": "/nix/store/s1qkj0ph…-bash-5.2p37/bin/bash",
              "env": {
                "HOME": "/homeless-shelter",
                "builder": "/nix/store/s1qkj0ph…-bash-5.2p37/bin/bash",
                "nativeBuildInputs": "/nix/store/l31j72f1…-version-check-hook",
                "out": "/nix/store/2yx2prgx…-hello-2.12.2"
                …
              },
              "inputPaths": [
                "/nix/store/14dciax3…-glibc-2.32-54-dev",
                "/nix/store/1azs5s8z…-gettext-0.21",
                …
              ],
              "outputs": {
                "out": "/nix/store/2yx2prgx…-hello-2.12.2"
              },
              "realStoreDir": "/nix/store",
              "storeDir": "/nix/store",
              "system": "aarch64-linux",
              "tmpDir": "/private/tmp/nix-build-hello-2.12.2.drv-0/build",
              "tmpDirInSandbox": "/build",
              "topTmpDir": "/private/tmp/nix-build-hello-2.12.2.drv-0",
              "version": 1
            }
        )",
        {},   // aliases
        true, // document default
        // NOTE(cole-h): even though we can make the experimental feature required here, the errors
        // are not as good (it just becomes a warning if you try to use this setting without the
        // experimental feature)
        //
        // With this commented out:
        //
        // error: experimental Nix feature 'external-builders' is disabled; add '--extra-experimental-features
        // external-builders' to enable it
        //
        // With this uncommented:
        //
        // warning: Ignoring setting 'external-builders' because experimental feature 'external-builders' is not enabled
        // error: Cannot build '/nix/store/vwsp4qd8…-opentofu-1.10.2.drv'.
        //        Reason: required system or feature not available
        //        Required system: 'aarch64-linux' with features {}
        //        Current system: 'aarch64-darwin' with features {apple-virt, benchmark, big-parallel, nixos-test}
        // Xp::ExternalBuilders
    };

    /**
     * Finds the first external derivation builder that supports this
     * derivation, or else returns a null pointer.
     */
    const ExternalBuilder * findExternalDerivationBuilderIfSupported(const Derivation & drv);
};

} // namespace nix

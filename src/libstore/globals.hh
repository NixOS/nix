#pragma once
///@file

#include "types.hh"
#include "config.hh"
#include "environment-variables.hh"
#include "experimental-features.hh"
#include "users.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

typedef enum { smEnabled, smRelaxed, smDisabled } SandboxMode;

struct MaxBuildJobsSetting : public BaseSetting<unsigned int>
{
    MaxBuildJobsSetting(Config * options,
        unsigned int def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : BaseSetting<unsigned int>(def, true, name, description, aliases)
    {
        options->addSetting(this);
    }

    unsigned int parse(const std::string & str) const override;
};

struct PluginFilesSetting : public BaseSetting<Paths>
{
    bool pluginsLoaded = false;

    PluginFilesSetting(Config * options,
        const Paths & def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : BaseSetting<Paths>(def, true, name, description, aliases)
    {
        options->addSetting(this);
    }

    Paths parse(const std::string & str) const override;
};

const uint32_t maxIdsPerBuild =
    #if __linux__
    1 << 16
    #else
    1
    #endif
    ;

class Settings : public Config {

    unsigned int getDefaultCores();

    StringSet getDefaultSystemFeatures();

    StringSet getDefaultExtraPlatforms();

    bool isWSL1();

    Path getDefaultSSLCertFile();

public:

    Settings();

    Path nixPrefix;

    /**
     * The directory where we store sources and derived files.
     */
    Path nixStore;

    Path nixDataDir; /* !!! fix */

    /**
     * The directory where we log various operations.
     */
    Path nixLogDir;

    /**
     * The directory where state is stored.
     */
    Path nixStateDir;

    /**
     * The directory where system configuration files are stored.
     */
    Path nixConfDir;

    /**
     * A list of user configuration files to load.
     */
    std::vector<Path> nixUserConfFiles;

    /**
     * The directory where the main programs are stored.
     */
    Path nixBinDir;

    /**
     * The directory where the man pages are stored.
     */
    Path nixManDir;

    /**
     * File name of the socket the daemon listens to.
     */
    Path nixDaemonSocketFile;

    Setting<std::string> storeUri{this, getEnv("NIX_REMOTE").value_or("auto"), "store",
        R"(
          The [URL of the Nix store](@docroot@/store/types/index.md#store-url-format)
          to use for most operations.
          See the
          [Store Types](@docroot@/store/types/index.md)
          section of the manual for supported store types and settings.
        )"};

    Setting<bool> keepFailed{this, false, "keep-failed",
        "Whether to keep temporary directories of failed builds."};

    Setting<bool> keepGoing{this, false, "keep-going",
        "Whether to keep building derivations when another build fails."};

    Setting<bool> tryFallback{
        this, false, "fallback",
        R"(
          If set to `true`, Nix will fall back to building from source if a
          binary substitute fails. This is equivalent to the `--fallback`
          flag. The default is `false`.
        )",
        {"build-fallback"}};

    /**
     * Whether to show build log output in real time.
     */
    bool verboseBuild = true;

    Setting<size_t> logLines{this, 25, "log-lines",
        "The number of lines of the tail of "
        "the log to show if a build fails."};

    MaxBuildJobsSetting maxBuildJobs{
        this, 1, "max-jobs",
        R"(
          Maximum number of jobs that Nix will try to build locally in parallel.

          The special value `auto` causes Nix to use the number of CPUs in your system.
          Use `0` to disable local builds and directly use the remote machines specified in [`builders`](#conf-builders).
          This will not affect derivations that have [`preferLocalBuild = true`](@docroot@/language/advanced-attributes.md#adv-attr-preferLocalBuild), which are always built locally.

          > **Note**
          >
          > The number of CPU cores to use for each build job is independently determined by the [`cores`](#conf-cores) setting.

          <!-- TODO(@fricklerhandwerk): would be good to have those shorthands for common options as part of the specification -->
          The setting can be overridden using the `--max-jobs` (`-j`) command line switch.
        )",
        {"build-max-jobs"}};

    Setting<unsigned int> maxSubstitutionJobs{
        this, 16, "max-substitution-jobs",
        R"(
          This option defines the maximum number of substitution jobs that Nix
          will try to run in parallel. The default is `16`. The minimum value
          one can choose is `1` and lower values will be interpreted as `1`.
        )",
        {"substitution-max-jobs"}};

    Setting<unsigned int> buildCores{
        this,
        getDefaultCores(),
        "cores",
        R"(
          Sets the value of the `NIX_BUILD_CORES` environment variable in the [invocation of the `builder` executable](@docroot@/language/derivations.md#builder-execution) of a derivation.
          The `builder` executable can use this variable to control its own maximum amount of parallelism.

          <!--
          FIXME(@fricklerhandwerk): I don't think this should even be mentioned here.
          A very generic example using `derivation` and `xargs` may be more appropriate to explain the mechanism.
          Using `mkDerivation` as an example requires being aware of that there are multiple independent layers that are completely opaque here.
          -->
          For instance, in Nixpkgs, if the attribute `enableParallelBuilding` for the `mkDerivation` build helper is set to `true`, it will pass the `-j${NIX_BUILD_CORES}` flag to GNU Make.

          The value `0` means that the `builder` should use all available CPU cores in the system.

          > **Note**
          >
          > The number of parallel local Nix build jobs is independently controlled with the [`max-jobs`](#conf-max-jobs) setting.
        )",
        {"build-cores"},
        // Don't document the machine-specific default value
        false};

    /**
     * Read-only mode.  Don't copy stuff to the store, don't change
     * the database.
     */
    bool readOnlyMode = false;

    Setting<std::string> thisSystem{
        this, SYSTEM, "system",
        R"(
          The system type of the current Nix installation.
          Nix will only build a given [derivation](@docroot@/language/derivations.md) locally when its `system` attribute equals any of the values specified here or in [`extra-platforms`](#conf-extra-platforms).

          The default value is set when Nix itself is compiled for the system it will run on.
          The following system types are widely used, as Nix is actively supported on these platforms:

          - `x86_64-linux`
          - `x86_64-darwin`
          - `i686-linux`
          - `aarch64-linux`
          - `aarch64-darwin`
          - `armv6l-linux`
          - `armv7l-linux`

          In general, you do not have to modify this setting.
          While you can force Nix to run a Darwin-specific `builder` executable on a Linux machine, the result would obviously be wrong.

          This value is available in the Nix language as
          [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
          if the
          [`eval-system`](#conf-eval-system)
          configuration option is set as the empty string.
        )"};

    Setting<time_t> maxSilentTime{
        this, 0, "max-silent-time",
        R"(
          This option defines the maximum number of seconds that a builder can
          go without producing any data on standard output or standard error.
          This is useful (for instance in an automated build system) to catch
          builds that are stuck in an infinite loop, or to catch remote builds
          that are hanging due to network problems. It can be overridden using
          the `--max-silent-time` command line switch.

          The value `0` means that there is no timeout. This is also the
          default.
        )",
        {"build-max-silent-time"}};

    Setting<time_t> buildTimeout{
        this, 0, "timeout",
        R"(
          This option defines the maximum number of seconds that a builder can
          run. This is useful (for instance in an automated build system) to
          catch builds that are stuck in an infinite loop but keep writing to
          their standard output or standard error. It can be overridden using
          the `--timeout` command line switch.

          The value `0` means that there is no timeout. This is also the
          default.
        )",
        {"build-timeout"}};

    Setting<Strings> buildHook{this, {}, "build-hook",
        R"(
          The path to the helper program that executes remote builds.

          Nix communicates with the build hook over `stdio` using a custom protocol to request builds that cannot be performed directly by the Nix daemon.
          The default value is the internal Nix binary that implements remote building.

          > **Important**
          >
          > Change this setting only if you really know what you’re doing.
        )"};

    Setting<std::string> builders{
        this, "@" + nixConfDir + "/machines", "builders",
        R"(
          A semicolon- or newline-separated list of build machines.

          In addition to the [usual ways of setting configuration options](@docroot@/command-ref/conf-file.md), the value can be read from a file by prefixing its absolute path with `@`.

          > **Example**
          >
          > This is the default setting:
          >
          > ```
          > builders = @/etc/nix/machines
          > ```

          Each machine specification consists of the following elements, separated by spaces.
          Only the first element is required.
          To leave a field at its default, set it to `-`.

          1. The URI of the remote store in the format `ssh://[username@]hostname`.

             > **Example**
             >
             > `ssh://nix@mac`

             For backward compatibility, `ssh://` may be omitted.
             The hostname may be an alias defined in `~/.ssh/config`.

          2. A comma-separated list of [Nix system types](@docroot@/contributing/hacking.md#system-type).
             If omitted, this defaults to the local platform type.

             > **Example**
             >
             > `aarch64-darwin`

             It is possible for a machine to support multiple platform types.

             > **Example**
             >
             > `i686-linux,x86_64-linux`

          3. The SSH identity file to be used to log in to the remote machine.
             If omitted, SSH will use its regular identities.

             > **Example**
             >
             > `/home/user/.ssh/id_mac`

          4. The maximum number of builds that Nix will execute in parallel on the machine.
             Typically this should be equal to the number of CPU cores.

          5. The “speed factor”, indicating the relative speed of the machine as a positive integer.
             If there are multiple machines of the right type, Nix will prefer the fastest, taking load into account.

          6. A comma-separated list of supported [system features](#conf-system-features).

             A machine will only be used to build a derivation if all the features in the derivation's [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.html#adv-attr-requiredSystemFeatures) attribute are supported by that machine.

          7. A comma-separated list of required [system features](#conf-system-features).

             A machine will only be used to build a derivation if all of the machine’s required features appear in the derivation’s [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.html#adv-attr-requiredSystemFeatures) attribute.

          8. The (base64-encoded) public host key of the remote machine.
             If omitted, SSH will use its regular `known_hosts` file.

             The value for this field can be obtained via `base64 -w0`.

          > **Example**
          >
          > Multiple builders specified on the command line:
          >
          > ```console
          > --builders 'ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd'
          > ```

          > **Example**
          >
          > This specifies several machines that can perform `i686-linux` builds:
          >
          > ```
          > nix@scratchy.labs.cs.uu.nl i686-linux /home/nix/.ssh/id_scratchy 8 1 kvm
          > nix@itchy.labs.cs.uu.nl    i686-linux /home/nix/.ssh/id_scratchy 8 2
          > nix@poochie.labs.cs.uu.nl  i686-linux /home/nix/.ssh/id_scratchy 1 2 kvm benchmark
          > ```
          >
          > However, `poochie` will only build derivations that have the attribute
          >
          > ```nix
          > requiredSystemFeatures = [ "benchmark" ];
          > ```
          >
          > or
          >
          > ```nix
          > requiredSystemFeatures = [ "benchmark" "kvm" ];
          > ```
          >
          > `itchy` cannot do builds that require `kvm`, but `scratchy` does support such builds.
          > For regular builds, `itchy` will be preferred over `scratchy` because it has a higher speed factor.

          For Nix to use substituters, the calling user must be in the [`trusted-users`](#conf-trusted-users) list.

          > **Note**
          >
          > A build machine must be accessible via SSH and have Nix installed.
          > `nix` must be available in `$PATH` for the user connecting over SSH.

          > **Warning**
          >
          > If you are building via the Nix daemon (default), the Nix daemon user account on the local machine (that is, `root`) requires access to a user account on the remote machine (not necessarily `root`).
          >
          > If you can’t or don’t want to configure `root` to be able to access the remote machine, set [`store`](#conf-store) to any [local store](@docroot@/store/types/local-store.html), e.g. by passing `--store /tmp` to the command on the local machine.

          To build only on remote machines and disable local builds, set [`max-jobs`](#conf-max-jobs) to 0.

          If you want the remote machines to use substituters, set [`builders-use-substitutes`](#conf-builders-use-substituters) to `true`.
        )",
        {}, false};

    Setting<bool> alwaysAllowSubstitutes{
        this, false, "always-allow-substitutes",
        R"(
          If set to `true`, Nix will ignore the [`allowSubstitutes`](@docroot@/language/advanced-attributes.md) attribute in derivations and always attempt to use [available substituters](#conf-substituters).
        )"};

    Setting<bool> buildersUseSubstitutes{
        this, false, "builders-use-substitutes",
        R"(
          If set to `true`, Nix will instruct [remote build machines](#conf-builders) to use their own [`substituters`](#conf-substituters) if available.

          It means that remote build hosts will fetch as many dependencies as possible from their own substituters (e.g, from `cache.nixos.org`) instead of waiting for the local machine to upload them all.
          This can drastically reduce build times if the network connection between the local machine and the remote build host is slow.
        )"};

    Setting<off_t> reservedSize{this, 8 * 1024 * 1024, "gc-reserved-space",
        "Amount of reserved disk space for the garbage collector."};

    Setting<bool> fsyncMetadata{
        this, true, "fsync-metadata",
        R"(
          If set to `true`, changes to the Nix store metadata (in
          `/nix/var/nix/db`) are synchronously flushed to disk. This improves
          robustness in case of system crashes, but reduces performance. The
          default is `true`.
        )"};

    Setting<bool> useSQLiteWAL{this, !isWSL1(), "use-sqlite-wal",
        "Whether SQLite should use WAL mode."};

    Setting<bool> syncBeforeRegistering{this, false, "sync-before-registering",
        "Whether to call `sync()` before registering a path as valid."};

    Setting<bool> useSubstitutes{
        this, true, "substitute",
        R"(
          If set to `true` (default), Nix will use binary substitutes if
          available. This option can be disabled to force building from
          source.
        )",
        {"build-use-substitutes"}};

    Setting<std::string> buildUsersGroup{
        this, "", "build-users-group",
        R"(
          This options specifies the Unix group containing the Nix build user
          accounts. In multi-user Nix installations, builds should not be
          performed by the Nix account since that would allow users to
          arbitrarily modify the Nix store and database by supplying specially
          crafted builders; and they cannot be performed by the calling user
          since that would allow him/her to influence the build result.

          Therefore, if this option is non-empty and specifies a valid group,
          builds will be performed under the user accounts that are a member
          of the group specified here (as listed in `/etc/group`). Those user
          accounts should not be used for any other purpose\!

          Nix will never run two builds under the same user account at the
          same time. This is to prevent an obvious security hole: a malicious
          user writing a Nix expression that modifies the build result of a
          legitimate Nix expression being built by another user. Therefore it
          is good to have as many Nix build user accounts as you can spare.
          (Remember: uids are cheap.)

          The build users should have permission to create files in the Nix
          store, but not delete them. Therefore, `/nix/store` should be owned
          by the Nix account, its group should be the group specified here,
          and its mode should be `1775`.

          If the build users group is empty, builds will be performed under
          the uid of the Nix process (that is, the uid of the caller if
          `NIX_REMOTE` is empty, the uid under which the Nix daemon runs if
          `NIX_REMOTE` is `daemon`). Obviously, this should not be used
          with a nix daemon accessible to untrusted clients.

          Defaults to `nixbld` when running as root, *empty* otherwise.
        )",
        {}, false};

    Setting<bool> autoAllocateUids{this, false, "auto-allocate-uids",
        R"(
          Whether to select UIDs for builds automatically, instead of using the
          users in `build-users-group`.

          UIDs are allocated starting at 872415232 (0x34000000) on Linux and 56930 on macOS.
        )", {}, true, Xp::AutoAllocateUids};

    Setting<uint32_t> startId{this,
        #if __linux__
        0x34000000,
        #else
        56930,
        #endif
        "start-id",
        "The first UID and GID to use for dynamic ID allocation."};

    Setting<uint32_t> uidCount{this,
        #if __linux__
        maxIdsPerBuild * 128,
        #else
        128,
        #endif
        "id-count",
        "The number of UIDs/GIDs to use for dynamic ID allocation."};

    #if __linux__
    Setting<bool> useCgroups{
        this, false, "use-cgroups",
        R"(
          Whether to execute builds inside cgroups.
          This is only supported on Linux.

          Cgroups are required and enabled automatically for derivations
          that require the `uid-range` system feature.
        )"};
    #endif

    Setting<bool> impersonateLinux26{this, false, "impersonate-linux-26",
        "Whether to impersonate a Linux 2.6 machine on newer kernels.",
        {"build-impersonate-linux-26"}};

    Setting<bool> keepLog{
        this, true, "keep-build-log",
        R"(
          If set to `true` (the default), Nix will write the build log of a
          derivation (i.e. the standard output and error of its builder) to
          the directory `/nix/var/log/nix/drvs`. The build log can be
          retrieved using the command `nix-store -l path`.
        )",
        {"build-keep-log"}};

    Setting<bool> compressLog{
        this, true, "compress-build-log",
        R"(
          If set to `true` (the default), build logs written to
          `/nix/var/log/nix/drvs` will be compressed on the fly using bzip2.
          Otherwise, they will not be compressed.
        )",
        {"build-compress-log"}};

    Setting<unsigned long> maxLogSize{
        this, 0, "max-build-log-size",
        R"(
          This option defines the maximum number of bytes that a builder can
          write to its stdout/stderr. If the builder exceeds this limit, it’s
          killed. A value of `0` (the default) means that there is no limit.
        )",
        {"build-max-log-size"}};

    Setting<unsigned int> pollInterval{this, 5, "build-poll-interval",
        "How often (in seconds) to poll for locks."};

    Setting<bool> gcKeepOutputs{
        this, false, "keep-outputs",
        R"(
          If `true`, the garbage collector will keep the outputs of
          non-garbage derivations. If `false` (default), outputs will be
          deleted unless they are GC roots themselves (or reachable from other
          roots).

          In general, outputs must be registered as roots separately. However,
          even if the output of a derivation is registered as a root, the
          collector will still delete store paths that are used only at build
          time (e.g., the C compiler, or source tarballs downloaded from the
          network). To prevent it from doing so, set this option to `true`.
        )",
        {"gc-keep-outputs"}};

    Setting<bool> gcKeepDerivations{
        this, true, "keep-derivations",
        R"(
          If `true` (default), the garbage collector will keep the derivations
          from which non-garbage store paths were built. If `false`, they will
          be deleted unless explicitly registered as a root (or reachable from
          other roots).

          Keeping derivation around is useful for querying and traceability
          (e.g., it allows you to ask with what dependencies or options a
          store path was built), so by default this option is on. Turn it off
          to save a bit of disk space (or a lot if `keep-outputs` is also
          turned on).
        )",
        {"gc-keep-derivations"}};

    Setting<bool> autoOptimiseStore{
        this, false, "auto-optimise-store",
        R"(
          If set to `true`, Nix automatically detects files in the store
          that have identical contents, and replaces them with hard links to
          a single copy. This saves disk space. If set to `false` (the
          default), you can still run `nix-store --optimise` to get rid of
          duplicate files.
        )"};

    Setting<bool> envKeepDerivations{
        this, false, "keep-env-derivations",
        R"(
          If `false` (default), derivations are not stored in Nix user
          environments. That is, the derivations of any build-time-only
          dependencies may be garbage-collected.

          If `true`, when you add a Nix derivation to a user environment, the
          path of the derivation is stored in the user environment. Thus, the
          derivation will not be garbage-collected until the user environment
          generation is deleted (`nix-env --delete-generations`). To prevent
          build-time-only dependencies from being collected, you should also
          turn on `keep-outputs`.

          The difference between this option and `keep-derivations` is that
          this one is “sticky”: it applies to any user environment created
          while this option was enabled, while `keep-derivations` only applies
          at the moment the garbage collector is run.
        )",
        {"env-keep-derivations"}};

    Setting<SandboxMode> sandboxMode{
        this,
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

    Setting<PathSet> sandboxPaths{
        this, {}, "sandbox-paths",
        R"(
          A list of paths bind-mounted into Nix sandbox environments. You can
          use the syntax `target=source` to mount a path in a different
          location in the sandbox; for instance, `/bin=/nix-bin` will mount
          the path `/nix-bin` as `/bin` inside the sandbox. If *source* is
          followed by `?`, then it is not an error if *source* does not exist;
          for example, `/dev/nvidiactl?` specifies that `/dev/nvidiactl` will
          only be mounted in the sandbox if it exists in the host filesystem.

          If the source is in the Nix store, then its closure will be added to
          the sandbox as well.

          Depending on how Nix was built, the default value for this option
          may be empty or provide `/bin/sh` as a bind-mount of `bash`.
        )",
        {"build-chroot-dirs", "build-sandbox-paths"}};

    Setting<bool> sandboxFallback{this, true, "sandbox-fallback",
        "Whether to disable sandboxing when the kernel doesn't allow it."};

    Setting<bool> requireDropSupplementaryGroups{this, isRootUser(), "require-drop-supplementary-groups",
        R"(
          Following the principle of least privilege,
          Nix will attempt to drop supplementary groups when building with sandboxing.

          However this can fail under some circumstances.
          For example, if the user lacks the `CAP_SETGID` capability.
          Search `setgroups(2)` for `EPERM` to find more detailed information on this.

          If you encounter such a failure, setting this option to `false` will let you ignore it and continue.
          But before doing so, you should consider the security implications carefully.
          Not dropping supplementary groups means the build sandbox will be less restricted than intended.

          This option defaults to `true` when the user is root
          (since `root` usually has permissions to call setgroups)
          and `false` otherwise.
        )"};

#if __linux__
    Setting<std::string> sandboxShmSize{
        this, "50%", "sandbox-dev-shm-size",
        R"(
            *Linux only*

            This option determines the maximum size of the `tmpfs` filesystem
            mounted on `/dev/shm` in Linux sandboxes. For the format, see the
            description of the `size` option of `tmpfs` in mount(8). The default
            is `50%`.
        )"};

    Setting<Path> sandboxBuildDir{this, "/build", "sandbox-build-dir",
        R"(
            *Linux only*

            The build directory inside the sandbox.

            This directory is backed by [`build-dir`](#conf-build-dir) on the host.
        )"};
#endif

    Setting<std::optional<Path>> buildDir{this, std::nullopt, "build-dir",
        R"(
            The directory on the host, in which derivations' temporary build directories are created.

            If not set, Nix will use the system temporary directory indicated by the `TMPDIR` environment variable.
            Note that builds are often performed by the Nix daemon, so its `TMPDIR` is used, and not that of the Nix command line interface.

            This is also the location where [`--keep-failed`](@docroot@/command-ref/opt-common.md#opt-keep-failed) leaves its files.

            If Nix runs without sandbox, or if the platform does not support sandboxing with bind mounts (e.g. macOS), then the [`builder`](@docroot@/language/derivations.md#attr-builder)'s environment will contain this directory, instead of the virtual location [`sandbox-build-dir`](#conf-sandbox-build-dir).
        )"};

    Setting<PathSet> allowedImpureHostPrefixes{this, {}, "allowed-impure-host-deps",
        "Which prefixes to allow derivations to ask for access to (primarily for Darwin)."};

#if __APPLE__
    Setting<bool> darwinLogSandboxViolations{this, false, "darwin-log-sandbox-violations",
        "Whether to log Darwin sandbox access violations to the system log."};
#endif

    Setting<bool> runDiffHook{
        this, false, "run-diff-hook",
        R"(
          If true, enable the execution of the `diff-hook` program.

          When using the Nix daemon, `run-diff-hook` must be set in the
          `nix.conf` configuration file, and cannot be passed at the command
          line.
        )"};

    OptionalPathSetting diffHook{
        this, std::nullopt, "diff-hook",
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

    Setting<Strings> trustedPublicKeys{
        this,
        {"cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="},
        "trusted-public-keys",
        R"(
          A whitespace-separated list of public keys.

          At least one of the following condition must be met
          for Nix to accept copying a store object from another
          Nix store (such as a [substituter](#conf-substituters)):

          - the store object has been signed using a key in the trusted keys list
          - the [`require-sigs`](#conf-require-sigs) option has been set to `false`
          - the store object is [content-addressed](@docroot@/glossary.md#gloss-content-addressed-store-object)
        )",
        {"binary-cache-public-keys"}};

    Setting<Strings> secretKeyFiles{
        this, {}, "secret-key-files",
        R"(
          A whitespace-separated list of files containing secret (private)
          keys. These are used to sign locally-built paths. They can be
          generated using `nix-store --generate-binary-cache-key`. The
          corresponding public key can be distributed to other users, who
          can add it to `trusted-public-keys` in their `nix.conf`.
        )"};

    Setting<unsigned int> tarballTtl{
        this, 60 * 60, "tarball-ttl",
        R"(
          The number of seconds a downloaded tarball is considered fresh. If
          the cached tarball is stale, Nix will check whether it is still up
          to date using the ETag header. Nix will download a new version if
          the ETag header is unsupported, or the cached ETag doesn't match.

          Setting the TTL to `0` forces Nix to always check if the tarball is
          up to date.

          Nix caches tarballs in `$XDG_CACHE_HOME/nix/tarballs`.

          Files fetched via `NIX_PATH`, `fetchGit`, `fetchMercurial`,
          `fetchTarball`, and `fetchurl` respect this TTL.
        )"};

    Setting<bool> requireSigs{
        this, true, "require-sigs",
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

    Setting<StringSet> extraPlatforms{
        this,
        getDefaultExtraPlatforms(),
        "extra-platforms",
        R"(
          System types of executables that can be run on this machine.

          Nix will only build a given [derivation](@docroot@/language/derivations.md) locally when its `system` attribute equals any of the values specified here or in the [`system` option](#conf-system).

          Setting this can be useful to build derivations locally on compatible machines:
          - `i686-linux` executables can be run on `x86_64-linux` machines (set by default)
          - `x86_64-darwin` executables can be run on macOS `aarch64-darwin` with Rosetta 2 (set by default where applicable)
          - `armv6` and `armv5tel` executables can be run on `armv7`
          - some `aarch64` machines can also natively run 32-bit ARM code
          - `qemu-user` may be used to support non-native platforms (though this
          may be slow and buggy)

          Build systems will usually detect the target platform to be the current physical system and therefore produce machine code incompatible with what may be intended in the derivation.
          You should design your derivation's `builder` accordingly and cross-check the results when using this option against natively-built versions of your derivation.
        )",
        {},
        // Don't document the machine-specific default value
        false};

    Setting<StringSet> systemFeatures{
        this,
        getDefaultSystemFeatures(),
        "system-features",
        R"(
          A set of system “features” supported by this machine.

          This complements the [`system`](#conf-system) and [`extra-platforms`](#conf-extra-platforms) configuration options and the corresponding [`system`](@docroot@/language/derivations.md#attr-system) attribute on derivations.

          A derivation can require system features in the [`requiredSystemFeatures` attribute](@docroot@/language/advanced-attributes.md#adv-attr-requiredSystemFeatures), and the machine to build the derivation must have them.

          System features are user-defined, but Nix sets the following defaults:

          - `apple-virt`

            Included on Darwin if virtualization is available.

          - `kvm`

            Included on Linux if `/dev/kvm` is accessible.

          - `nixos-test`, `benchmark`, `big-parallel`

            These historical pseudo-features are always enabled for backwards compatibility, as they are used in Nixpkgs to route Hydra builds to specific machines.

          - `ca-derivations`

            Included by default if the [`ca-derivations` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-ca-derivations) is enabled.

            This system feature is implicitly required by derivations with the [`__contentAddressed` attribute](@docroot@/language/advanced-attributes.md#adv-attr-__contentAddressed).

          - `recursive-nix`

            Included by default if the [`recursive-nix` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-recursive-nix) is enabled.

          - `uid-range`

            On Linux, Nix can run builds in a user namespace where they run as root (UID 0) and have 65,536 UIDs available.
            This is primarily useful for running containers such as `systemd-nspawn` inside a Nix build. For an example, see [`tests/systemd-nspawn/nix`][nspawn].

            [nspawn]: https://github.com/NixOS/nix/blob/67bcb99700a0da1395fa063d7c6586740b304598/tests/systemd-nspawn.nix.

            Included by default on Linux if the [`auto-allocate-uids`](#conf-auto-allocate-uids) setting is enabled.
        )",
        {},
        // Don't document the machine-specific default value
        false};

    Setting<Strings> substituters{
        this,
        Strings{"https://cache.nixos.org/"},
        "substituters",
        R"(
          A list of [URLs of Nix stores](@docroot@/store/types/index.md#store-url-format) to be used as substituters, separated by whitespace.
          A substituter is an additional [store](@docroot@/glossary.md#gloss-store) from which Nix can obtain [store objects](@docroot@/glossary.md#gloss-store-object) instead of building them.

          Substituters are tried based on their priority value, which each substituter can set independently.
          Lower value means higher priority.
          The default is `https://cache.nixos.org`, which has a priority of 40.

          At least one of the following conditions must be met for Nix to use a substituter:

          - The substituter is in the [`trusted-substituters`](#conf-trusted-substituters) list
          - The user calling Nix is in the [`trusted-users`](#conf-trusted-users) list

          In addition, each store path should be trusted as described in [`trusted-public-keys`](#conf-trusted-public-keys)
        )",
        {"binary-caches"}};

    Setting<StringSet> trustedSubstituters{
        this, {}, "trusted-substituters",
        R"(
          A list of [Nix store URLs](@docroot@/store/types/index.md#store-url-format), separated by whitespace.
          These are not used by default, but users of the Nix daemon can enable them by specifying [`substituters`](#conf-substituters).

          Unprivileged users (those set in only [`allowed-users`](#conf-allowed-users) but not [`trusted-users`](#conf-trusted-users)) can pass as `substituters` only those URLs listed in `trusted-substituters`.
        )",
        {"trusted-binary-caches"}};

    Setting<unsigned int> ttlNegativeNarInfoCache{
        this, 3600, "narinfo-cache-negative-ttl",
        R"(
          The TTL in seconds for negative lookups.
          If a store path is queried from a [substituter](#conf-substituters) but was not found, there will be a negative lookup cached in the local disk cache database for the specified duration.

          Set to `0` to force updating the lookup cache.

          To wipe the lookup cache completely:

          ```shell-session
          $ rm $HOME/.cache/nix/binary-cache-v*.sqlite*
          # rm /root/.cache/nix/binary-cache-v*.sqlite*
          ```
        )"};

    Setting<unsigned int> ttlPositiveNarInfoCache{
        this, 30 * 24 * 3600, "narinfo-cache-positive-ttl",
        R"(
          The TTL in seconds for positive lookups. If a store path is queried
          from a substituter, the result of the query will be cached in the
          local disk cache database including some of the NAR metadata. The
          default TTL is a month, setting a shorter TTL for positive lookups
          can be useful for binary caches that have frequent garbage
          collection, in which case having a more frequent cache invalidation
          would prevent trying to pull the path again and failing with a hash
          mismatch if the build isn't reproducible.
        )"};

    Setting<bool> printMissing{this, true, "print-missing",
        "Whether to print what paths need to be built or downloaded."};

    Setting<std::string> preBuildHook{
        this, "", "pre-build-hook",
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

    Setting<std::string> postBuildHook{
        this, "", "post-build-hook",
        R"(
          Optional. The path to a program to execute after each build.

          This option is only settable in the global `nix.conf`, or on the
          command line by trusted users.

          When using the nix-daemon, the daemon executes the hook as `root`.
          If the nix-daemon is not involved, the hook runs as the user
          executing the nix-build.

            - The hook executes after an evaluation-time build.

            - The hook does not execute on substituted paths.

            - The hook's output always goes to the user's terminal.

            - If the hook fails, the build succeeds but no further builds
              execute.

            - The hook executes synchronously, and blocks other builds from
              progressing while it runs.

          The program executes with no arguments. The program's environment
          contains the following environment variables:

            - `DRV_PATH`
              The derivation for the built paths.

              Example:
              `/nix/store/5nihn1a7pa8b25l9zafqaqibznlvvp3f-bash-4.4-p23.drv`

            - `OUT_PATHS`
              Output paths of the built derivation, separated by a space
              character.

              Example:
              `/nix/store/zf5lbh336mnzf1nlswdn11g4n2m8zh3g-bash-4.4-p23-dev
              /nix/store/rjxwxwv1fpn9wa2x5ssk5phzwlcv4mna-bash-4.4-p23-doc
              /nix/store/6bqvbzjkcp9695dq0dpl5y43nvy37pq1-bash-4.4-p23-info
              /nix/store/r7fng3kk3vlpdlh2idnrbn37vh4imlj2-bash-4.4-p23-man
              /nix/store/xfghy8ixrhz3kyy6p724iv3cxji088dx-bash-4.4-p23`.
        )"};

    Setting<unsigned int> downloadSpeed {
        this, 0, "download-speed",
        R"(
          Specify the maximum transfer rate in kilobytes per second you want
          Nix to use for downloads.
        )"};

    Setting<std::string> netrcFile{
        this, fmt("%s/%s", nixConfDir, "netrc"), "netrc-file",
        R"(
          If set to an absolute path to a `netrc` file, Nix will use the HTTP
          authentication credentials in this file when trying to download from
          a remote host through HTTP or HTTPS. Defaults to
          `$NIX_CONF_DIR/netrc`.

          The `netrc` file consists of a list of accounts in the following
          format:

              machine my-machine
              login my-username
              password my-password

          For the exact syntax, see [the `curl`
          documentation](https://ec.haxx.se/usingcurl-netrc.html).

          > **Note**
          >
          > This must be an absolute path, and `~` is not resolved. For
          > example, `~/.netrc` won't resolve to your home directory's
          > `.netrc`.
        )"};

    Setting<Path> caFile{
        this, getDefaultSSLCertFile(), "ssl-cert-file",
        R"(
          The path of a file containing CA certificates used to
          authenticate `https://` downloads. Nix by default will use
          the first of the following files that exists:

          1. `/etc/ssl/certs/ca-certificates.crt`
          2. `/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt`

          The path can be overridden by the following environment
          variables, in order of precedence:

          1. `NIX_SSL_CERT_FILE`
          2. `SSL_CERT_FILE`
        )"};

#if __linux__
    Setting<bool> filterSyscalls{
        this, true, "filter-syscalls",
        R"(
          Whether to prevent certain dangerous system calls, such as
          creation of setuid/setgid files or adding ACLs or extended
          attributes. Only disable this if you're aware of the
          security implications.
        )"};

    Setting<bool> allowNewPrivileges{
        this, false, "allow-new-privileges",
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

#if HAVE_ACL_SUPPORT
    Setting<StringSet> ignoredAcls{
        this, {"security.selinux", "system.nfs4_acl", "security.csm"}, "ignored-acls",
        R"(
          A list of ACLs that should be ignored, normally Nix attempts to
          remove all ACLs from files and directories in the Nix store, but
          some ACLs like `security.selinux` or `system.nfs4_acl` can't be
          removed even by root. Therefore it's best to just ignore them.
        )"};
#endif

    Setting<Strings> hashedMirrors{
        this, {}, "hashed-mirrors",
        R"(
          A list of web servers used by `builtins.fetchurl` to obtain files by
          hash. Given a hash algorithm *ha* and a base-16 hash *h*, Nix will try to
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
          first. If it is not available there, if will try the original URI.
        )"};

    Setting<uint64_t> minFree{
        this, 0, "min-free",
        R"(
          When free disk space in `/nix/store` drops below `min-free` during a
          build, Nix performs a garbage-collection until `max-free` bytes are
          available or there is no more garbage. A value of `0` (the default)
          disables this feature.
        )"};

    Setting<uint64_t> maxFree{
        this, std::numeric_limits<uint64_t>::max(), "max-free",
        R"(
          When a garbage collection is triggered by the `min-free` option, it
          stops as soon as `max-free` bytes are available. The default is
          infinity (i.e. delete all garbage).
        )"};

    Setting<uint64_t> minFreeCheckInterval{this, 5, "min-free-check-interval",
        "Number of seconds between checking free disk space."};

    PluginFilesSetting pluginFiles{
        this, {}, "plugin-files",
        R"(
          A list of plugin files to be loaded by Nix. Each of these files will
          be dlopened by Nix. If they contain the symbol `nix_plugin_entry()`,
          this symbol will be called. Alternatively, they can affect execution
          through static initialization. In particular, these plugins may construct
          static instances of RegisterPrimOp to add new primops or constants to the
          expression language, RegisterStoreImplementation to add new store
          implementations, RegisterCommand to add new subcommands to the `nix`
          command, and RegisterSetting to add new nix config settings. See the
          constructors for those types for more details.

          Warning! These APIs are inherently unstable and may change from
          release to release.

          Since these files are loaded into the same address space as Nix
          itself, they must be DSOs compatible with the instance of Nix
          running at the time (i.e. compiled against the same headers, not
          linked to any incompatible libraries). They should not be linked to
          any Nix libs directly, as those will be available already at load
          time.

          If an entry in the list is a directory, all files in the directory
          are loaded as plugins (non-recursively).
        )"};

    Setting<size_t> narBufferSize{this, 32 * 1024 * 1024, "nar-buffer-size",
        "Maximum size of NARs before spilling them to disk."};

    Setting<bool> allowSymlinkedStore{
        this, false, "allow-symlinked-store",
        R"(
          If set to `true`, Nix will stop complaining if the store directory
          (typically /nix/store) contains symlink components.

          This risks making some builds "impure" because builders sometimes
          "canonicalise" paths by resolving all symlink components. Problems
          occur if those builds are then deployed to machines where /nix/store
          resolves to a different location from that of the build machine. You
          can enable this setting if you are sure you're not going to do that.
        )"};

    Setting<bool> useXDGBaseDirectories{
        this, false, "use-xdg-base-directories",
        R"(
          If set to `true`, Nix will conform to the [XDG Base Directory Specification] for files in `$HOME`.
          The environment variables used to implement this are documented in the [Environment Variables section](@docroot@/command-ref/env-common.md).

          [XDG Base Directory Specification]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

          > **Warning**
          > This changes the location of some well-known symlinks that Nix creates, which might break tools that rely on the old, non-XDG-conformant locations.

          In particular, the following locations change:

          | Old               | New                            |
          |-------------------|--------------------------------|
          | `~/.nix-profile`  | `$XDG_STATE_HOME/nix/profile`  |
          | `~/.nix-defexpr`  | `$XDG_STATE_HOME/nix/defexpr`  |
          | `~/.nix-channels` | `$XDG_STATE_HOME/nix/channels` |

          If you already have Nix installed and are using [profiles](@docroot@/package-management/profiles.md) or [channels](@docroot@/command-ref/nix-channel.md), you should migrate manually when you enable this option.
          If `$XDG_STATE_HOME` is not set, use `$HOME/.local/state/nix` instead of `$XDG_STATE_HOME/nix`.
          This can be achieved with the following shell commands:

          ```sh
          nix_state_home=${XDG_STATE_HOME-$HOME/.local/state}/nix
          mkdir -p $nix_state_home
          mv $HOME/.nix-profile $nix_state_home/profile
          mv $HOME/.nix-defexpr $nix_state_home/defexpr
          mv $HOME/.nix-channels $nix_state_home/channels
          ```
        )"
    };

    Setting<StringMap> impureEnv {this, {}, "impure-env",
        R"(
          A list of items, each in the format of:

          - `name=value`: Set environment variable `name` to `value`.

          If the user is trusted (see `trusted-users` option), when building
          a fixed-output derivation, environment variables set in this option
          will be passed to the builder if they are listed in [`impureEnvVars`](@docroot@/language/advanced-attributes.md##adv-attr-impureEnvVars).

          This option is useful for, e.g., setting `https_proxy` for
          fixed-output derivations and in a multi-user Nix installation, or
          setting private access tokens when fetching a private repository.
        )",
        {}, // aliases
        true, // document default
        Xp::ConfigurableImpureEnv
    };

    Setting<std::string> upgradeNixStorePathUrl{
        this,
        "https://github.com/NixOS/nixpkgs/raw/master/nixos/modules/installer/tools/nix-fallback-paths.nix",
        "upgrade-nix-store-path-url",
        R"(
          Used by `nix upgrade-nix`, the URL of the file that contains the
          store paths of the latest Nix release.
        )"
    };
};


// FIXME: don't use a global variable.
extern Settings settings;

/**
 * This should be called after settings are initialized, but before
 * anything else
 */
void initPlugins();

void loadConfFile();

// Used by the Settings constructor
std::vector<Path> getUserConfigFiles();

extern const std::string nixVersion;

/**
 * NB: This is not sufficient. You need to call initNix()
 */
void initLibStore();

/**
 * It's important to initialize before doing _anything_, which is why we
 * call upon the programmer to handle this correctly. However, we only add
 * this in a key locations, so as not to litter the code.
 */
void assertLibStoreInitialized();

}

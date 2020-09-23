#pragma once

#include "types.hh"
#include "config.hh"
#include "util.hh"

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
        : BaseSetting<unsigned int>(def, name, description, aliases)
    {
        options->addSetting(this);
    }

    void set(const std::string & str) override;
};

class Settings : public Config {

    unsigned int getDefaultCores();

    StringSet getDefaultSystemFeatures();

    bool isWSL1();

public:

    Settings();

    Path nixPrefix;

    /* The directory where we store sources and derived files. */
    Path nixStore;

    Path nixDataDir; /* !!! fix */

    /* The directory where we log various operations. */
    Path nixLogDir;

    /* The directory where state is stored. */
    Path nixStateDir;

    /* The directory where system configuration files are stored. */
    Path nixConfDir;

    /* A list of user configuration files to load. */
    std::vector<Path> nixUserConfFiles;

    /* The directory where internal helper programs are stored. */
    Path nixLibexecDir;

    /* The directory where the main programs are stored. */
    Path nixBinDir;

    /* The directory where the man pages are stored. */
    Path nixManDir;

    /* File name of the socket the daemon listens to.  */
    Path nixDaemonSocketFile;

    Setting<std::string> storeUri{this, getEnv("NIX_REMOTE").value_or("auto"), "store",
        "The default Nix store to use."};

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

    /* Whether to show build log output in real time. */
    bool verboseBuild = true;

    Setting<size_t> logLines{this, 10, "log-lines",
        "If `verbose-build` is false, the number of lines of the tail of "
        "the log to show if a build fails."};

    MaxBuildJobsSetting maxBuildJobs{
        this, 1, "max-jobs",
        R"(
          This option defines the maximum number of jobs that Nix will try to
          build in parallel. The default is `1`. The special value `auto`
          causes Nix to use the number of CPUs in your system. `0` is useful
          when using remote builders to prevent any local builds (except for
          `preferLocalBuild` derivation attribute which executes locally
          regardless). It can be overridden using the `--max-jobs` (`-j`)
          command line switch.
        )",
        {"build-max-jobs"}};

    Setting<unsigned int> buildCores{
        this, getDefaultCores(), "cores",
        R"(
          Sets the value of the `NIX_BUILD_CORES` environment variable in the
          invocation of builders. Builders can use this variable at their
          discretion to control the maximum amount of parallelism. For
          instance, in Nixpkgs, if the derivation attribute
          `enableParallelBuilding` is set to `true`, the builder passes the
          `-jN` flag to GNU Make. It can be overridden using the `--cores`
          command line switch and defaults to `1`. The value `0` means that
          the builder should use all available CPU cores in the system.
        )",
        {"build-cores"}};

    /* Read-only mode.  Don't copy stuff to the store, don't change
       the database. */
    bool readOnlyMode = false;

    Setting<std::string> thisSystem{
        this, SYSTEM, "system",
        R"(
          This option specifies the canonical Nix system name of the current
          installation, such as `i686-linux` or `x86_64-darwin`. Nix can only
          build derivations whose `system` attribute equals the value
          specified here. In general, it never makes sense to modify this
          value from its default, since you can use it to ‘lie’ about the
          platform you are building on (e.g., perform a Mac OS build on a
          Linux machine; the result would obviously be wrong). It only makes
          sense if the Nix binaries can run on multiple platforms, e.g.,
          ‘universal binaries’ that run on `x86_64-linux` and `i686-linux`.

          It defaults to the canonical Nix system name detected by `configure`
          at build time.
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

    PathSetting buildHook{this, true, nixLibexecDir + "/nix/build-remote", "build-hook",
        "The path of the helper program that executes builds to remote machines."};

    Setting<std::string> builders{
        this, "@" + nixConfDir + "/machines", "builders",
        "A semicolon-separated list of build machines, in the format of `nix.machines`."};

    Setting<bool> buildersUseSubstitutes{
        this, false, "builders-use-substitutes",
        R"(
          If set to `true`, Nix will instruct remote build machines to use
          their own binary substitutes if available. In practical terms, this
          means that remote hosts will fetch as many build dependencies as
          possible from their own substitutes (e.g, from `cache.nixos.org`),
          instead of waiting for this host to upload them all. This can
          drastically reduce build times if the network connection between
          this computer and the remote build host is slow.
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
          `NIX_REMOTE` is `daemon`). Obviously, this should not be used in
          multi-user settings with untrusted users.
        )"};

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

    /* When buildRepeat > 0 and verboseBuild == true, whether to print
       repeated builds (i.e. builds other than the first one) to
       stderr. Hack to prevent Hydra logs from being polluted. */
    bool printRepeatedBuilds = true;

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

    /* Whether to lock the Nix client and worker to the same CPU. */
    bool lockCPU;

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

          Depending on how Nix was built, the default value for this option
          may be empty or provide `/bin/sh` as a bind-mount of `bash`.
        )",
        {"build-chroot-dirs", "build-sandbox-paths"}};

    Setting<bool> sandboxFallback{this, true, "sandbox-fallback",
        "Whether to disable sandboxing when the kernel doesn't allow it."};

    Setting<PathSet> extraSandboxPaths{
        this, {}, "extra-sandbox-paths",
        R"(
          A list of additional paths appended to `sandbox-paths`. Useful if
          you want to extend its default value.
        )",
        {"build-extra-chroot-dirs", "build-extra-sandbox-paths"}};

    Setting<size_t> buildRepeat{
        this, 0, "repeat",
        R"(
          How many times to repeat builds to check whether they are
          deterministic. The default value is 0. If the value is non-zero,
          every build is repeated the specified number of times. If the
          contents of any of the runs differs from the previous ones and
          `enforce-determinism` is true, the build is rejected and the
          resulting store paths are not registered as “valid” in Nix’s
          database.
        )",
        {"build-repeat"}};

#if __linux__
    Setting<std::string> sandboxShmSize{
        this, "50%", "sandbox-dev-shm-size",
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
        this, false, "run-diff-hook",
        R"(
          If true, enable the execution of the `diff-hook` program.

          When using the Nix daemon, `run-diff-hook` must be set in the
          `nix.conf` configuration file, and cannot be passed at the command
          line.
        )"};

    PathSetting diffHook{
        this, true, "", "diff-hook",
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

    Setting<bool> enforceDeterminism{
        this, true, "enforce-determinism",
        "Whether to fail if repeated builds produce different output. See `repeat`."};

    Setting<Strings> trustedPublicKeys{
        this,
        {"cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="},
        "trusted-public-keys",
        R"(
          A whitespace-separated list of public keys. When paths are copied
          from another Nix store (such as a binary cache), they must be
          signed with one of these keys. For example:
          `cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=
          hydra.nixos.org-1:CNHJZBh9K4tP3EKF6FkkgeVYsS3ohTl+oS0Qa8bezVs=`.
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
          cache) must have a valid signature, that is, be signed using one of
          the keys listed in `trusted-public-keys` or `secret-key-files`. Set
          to `false` to disable signature checking.
        )"};

    Setting<StringSet> extraPlatforms{
        this,
        std::string{SYSTEM} == "x86_64-linux" && !isWSL1() ? StringSet{"i686-linux"} : StringSet{},
        "extra-platforms",
        R"(
          Platforms other than the native one which this machine is capable of
          building for. This can be useful for supporting additional
          architectures on compatible machines: i686-linux can be built on
          x86\_64-linux machines (and the default for this setting reflects
          this); armv7 is backwards-compatible with armv6 and armv5tel; some
          aarch64 machines can also natively run 32-bit ARM code; and
          qemu-user may be used to support non-native platforms (though this
          may be slow and buggy). Most values for this are not enabled by
          default because build systems will often misdetect the target
          platform and generate incompatible code, so you may wish to
          cross-check the results of using this option against proper
          natively-built versions of your derivations.
        )"};

    Setting<StringSet> systemFeatures{
        this, getDefaultSystemFeatures(),
        "system-features",
        R"(
          A set of system “features” supported by this machine, e.g. `kvm`.
          Derivations can express a dependency on such features through the
          derivation attribute `requiredSystemFeatures`. For example, the
          attribute

              requiredSystemFeatures = [ "kvm" ];

          ensures that the derivation can only be built on a machine with the
          `kvm` feature.

          This setting by default includes `kvm` if `/dev/kvm` is accessible,
          and the pseudo-features `nixos-test`, `benchmark` and `big-parallel`
          that are used in Nixpkgs to route builds to specific machines.
        )"};

    Setting<Strings> substituters{
        this,
        nixStore == "/nix/store" ? Strings{"https://cache.nixos.org/"} : Strings(),
        "substituters",
        R"(
          A list of URLs of substituters, separated by whitespace. The default
          is `https://cache.nixos.org`.
        )",
        {"binary-caches"}};

    // FIXME: provide a way to add to option values.
    Setting<Strings> extraSubstituters{
        this, {}, "extra-substituters",
        R"(
          Additional binary caches appended to those specified in
          `substituters`. When used by unprivileged users, untrusted
          substituters (i.e. those not listed in `trusted-substituters`) are
          silently ignored.
        )",
        {"extra-binary-caches"}};

    Setting<StringSet> trustedSubstituters{
        this, {}, "trusted-substituters",
        R"(
          A list of URLs of substituters, separated by whitespace. These are
          not used by default, but can be enabled by users of the Nix daemon
          by specifying `--option substituters urls` on the command
          line. Unprivileged users are only allowed to pass a subset of the
          URLs listed in `substituters` and `trusted-substituters`.
        )",
        {"trusted-binary-caches"}};

    Setting<Strings> trustedUsers{
        this, {"root"}, "trusted-users",
        R"(
          A list of names of users (separated by whitespace) that have
          additional rights when connecting to the Nix daemon, such as the
          ability to specify additional binary caches, or to import unsigned
          NARs. You can also specify groups by prefixing them with `@`; for
          instance, `@wheel` means all users in the `wheel` group. The default
          is `root`.

          > **Warning**
          > 
          > Adding a user to `trusted-users` is essentially equivalent to
          > giving that user root access to the system. For example, the user
          > can set `sandbox-paths` and thereby obtain read access to
          > directories that are otherwise inacessible to them.
        )"};

    Setting<unsigned int> ttlNegativeNarInfoCache{
        this, 3600, "narinfo-cache-negative-ttl",
        R"(
          The TTL in seconds for negative lookups. If a store path is queried
          from a substituter but was not found, there will be a negative
          lookup cached in the local disk cache database for the specified
          duration.
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

    /* ?Who we trust to use the daemon in safe ways */
    Setting<Strings> allowedUsers{
        this, {"*"}, "allowed-users",
        R"(
          A list of names of users (separated by whitespace) that are allowed
          to connect to the Nix daemon. As with the `trusted-users` option,
          you can specify groups by prefixing them with `@`. Also, you can
          allow all users by specifying `*`. The default is `*`.

          Note that trusted users are always allowed to connect.
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

            - `extra-sandbox-paths`  
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

    /* Path to the SSL CA file used */
    Path caFile;

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

    Setting<Strings> hashedMirrors{
        this, {}, "hashed-mirrors",
        R"(
          A list of web servers used by `builtins.fetchurl` to obtain files by
          hash. The default is `http://tarballs.nixos.org/`. Given a hash type
          *ht* and a base-16 hash *h*, Nix will try to download the file from
          *hashed-mirror*/*ht*/*h*. This allows files to be downloaded even if
          they have disappeared from their original URI. For example, given
          the default mirror `http://tarballs.nixos.org/`, when building the
          derivation

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

    Setting<Paths> pluginFiles{
        this, {}, "plugin-files",
        R"(
          A list of plugin files to be loaded by Nix. Each of these files will
          be dlopened by Nix, allowing them to affect execution through static
          initialization. In particular, these plugins may construct static
          instances of RegisterPrimOp to add new primops or constants to the
          expression language, RegisterStoreImplementation to add new store
          implementations, RegisterCommand to add new subcommands to the `nix`
          command, and RegisterSetting to add new nix config settings. See the
          constructors for those types for more details.

          Since these files are loaded into the same address space as Nix
          itself, they must be DSOs compatible with the instance of Nix
          running at the time (i.e. compiled against the same headers, not
          linked to any incompatible libraries). They should not be linked to
          any Nix libs directly, as those will be available already at load
          time.

          If an entry in the list is a directory, all files in the directory
          are loaded as plugins (non-recursively).
        )"};

    Setting<std::string> githubAccessToken{this, "", "github-access-token",
        "GitHub access token to get access to GitHub data through the GitHub API for `github:<..>` flakes."};

    Setting<bool> allowExperimentalFeatures{this, true, "allow-experimental-features",
        "Whether the use of experimental features other than those listed in "
        "the option 'experimental-features' gives a warning rather than fatal error."};

    Setting<Strings> experimentalFeatures{this, {}, "experimental-features",
        "Experimental Nix features to enable."};

    bool isExperimentalFeatureEnabled(const std::string & name);

    void requireExperimentalFeature(const std::string & name);

    Setting<bool> allowDirty{this, true, "allow-dirty",
        "Whether to allow dirty Git/Mercurial trees."};

    Setting<bool> warnDirty{this, true, "warn-dirty",
        "Whether to warn about dirty Git/Mercurial trees."};

    Setting<size_t> narBufferSize{this, 32 * 1024 * 1024, "nar-buffer-size",
        "Maximum size of NARs before spilling them to disk."};

    Setting<std::string> flakeRegistry{this, "https://github.com/NixOS/flake-registry/raw/master/flake-registry.json", "flake-registry",
        "Path or URI of the global flake registry."};

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
};


// FIXME: don't use a global variable.
extern Settings settings;

/* This should be called after settings are initialized, but before
   anything else */
void initPlugins();

void loadConfFile();

// Used by the Settings constructor
std::vector<Path> getUserConfigFiles();

extern const string nixVersion;

}

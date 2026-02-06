#pragma once
///@file

#include <sys/types.h>

#include "nix/util/types.hh"
#include "nix/util/configuration.hh"
#include "nix/util/environment-variables.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/store/local-settings.hh"

#include "nix/util/compression-settings.hh"
#include "nix/store/config.hh"

namespace nix {

/**
 * Custom setting subclass for build log compression that handles
 * backward compatibility with the old boolean values.
 *
 * Accepts `true` (mapped to `bzip2`), `false` (mapped to `none`),
 * or any compression algorithm name.
 */
struct BuildLogCompressionSetting : public BaseSetting<CompressionAlgo>
{
    BuildLogCompressionSetting(
        Config * options,
        CompressionAlgo def,
        const std::string & name,
        const std::string & description,
        const StringSet & aliases = {})
        : BaseSetting<CompressionAlgo>(def, true, name, description, aliases)
    {
        options->addSetting(this);
    }

    CompressionAlgo parse(const std::string & str) const override;

    void convertToArg(Args & args, const std::string & category) override;

    bool enabled() const
    {
        return value != CompressionAlgo::none;
    }
};

struct MaxBuildJobsSetting : public BaseSetting<unsigned int>
{
    MaxBuildJobsSetting(
        Config * options,
        unsigned int def,
        const std::string & name,
        const std::string & description,
        const StringSet & aliases = {})
        : BaseSetting<unsigned int>(def, true, name, description, aliases)
    {
        options->addSetting(this);
    }

    unsigned int parse(const std::string & str) const override;
};

struct LogFileSettings : public virtual Config
{
    /**
     * The directory where we log various operations.
     */
    const Path nixLogDir;

protected:
    LogFileSettings();

public:
    Setting<bool> keepLog{
        this,
        true,
        "keep-build-log",
        R"(
          If set to `true` (the default), Nix writes the build log of a
          derivation (i.e. the standard output and error of its builder) to
          the directory `/nix/var/log/nix/drvs`. The build log can be
          retrieved using the command `nix-store -l path`.
        )",
        {"build-keep-log"}};

    BuildLogCompressionSetting compressLog{
        this,
        CompressionAlgo::bzip2,
        "compress-build-log",
        R"(
          Compression method for build logs written to `/nix/var/log/nix/drvs`.
          Valid values are `none` (no compression), `bzip2` (the default),
          `zstd`, `xz`, `gzip`, `lz4`, or `br`.
          For backward compatibility, `true` is equivalent to `bzip2`
          and `false` is equivalent to `none`.
        )",
        {"build-compress-log"}};
};

class Settings : public virtual Config, private LocalSettings, private LogFileSettings
{
    StringSet getDefaultSystemFeatures();

    StringSet getDefaultExtraPlatforms();

    bool isWSL1();

    Path getDefaultSSLCertFile();

public:

    Settings();

    using ExternalBuilders = std::vector<ExternalBuilder>;

    /**
     * Get the local store settings.
     */
    LocalSettings & getLocalSettings()
    {
        return *this;
    }

    const LocalSettings & getLocalSettings() const
    {
        return *this;
    }

    /**
     * Get the log file settings.
     */
    LogFileSettings & getLogFileSettings()
    {
        return *this;
    }

    const LogFileSettings & getLogFileSettings() const
    {
        return *this;
    }

    static unsigned int getDefaultCores();

    /**
     * The directory where we store sources and derived files.
     */
    Path nixStore;

    /**
     * The directory where state is stored.
     */
    Path nixStateDir;

    /**
     * The directory where system configuration files are stored.
     */
    std::filesystem::path nixConfDir;

    /**
     * A list of user configuration files to load.
     */
    std::vector<Path> nixUserConfFiles;

    /**
     * File name of the socket the daemon listens to.
     */
    Path nixDaemonSocketFile;

    Setting<std::string> storeUri{
        this,
        getEnv("NIX_REMOTE").value_or("auto"),
        "store",
        R"(
          The [URL of the Nix store](@docroot@/store/types/index.md#store-url-format)
          to use for most operations.
          See the
          [Store Types](@docroot@/store/types/index.md)
          section of the manual for supported store types and settings.
        )"};

    Setting<bool> useSQLiteWAL{this, !isWSL1(), "use-sqlite-wal", "Whether SQLite should use WAL mode."};

    Setting<bool> keepFailed{this, false, "keep-failed", "Whether to keep temporary directories of failed builds."};

    Setting<bool> keepGoing{
        this, false, "keep-going", "Whether to keep building derivations when another build fails."};

    Setting<bool> tryFallback{
        this,
        false,
        "fallback",
        R"(
          If set to `true`, Nix falls back to building from source if a
          binary substitute fails. This is equivalent to the `--fallback`
          flag. The default is `false`.
        )",
        {"build-fallback"}};

    /**
     * Whether to show build log output in real time.
     */
    bool verboseBuild = true;

    Setting<size_t> logLines{
        this,
        25,
        "log-lines",
        "The number of lines of the tail of "
        "the log to show if a build fails."};

    MaxBuildJobsSetting maxBuildJobs{
        this,
        1,
        "max-jobs",
        R"(
          Maximum number of jobs that Nix tries to build locally in parallel.

          The special value `auto` causes Nix to use the number of CPUs in your system.
          Use `0` to disable local builds and directly use the remote machines specified in [`builders`](#conf-builders).
          This doesn't affect derivations that have [`preferLocalBuild = true`](@docroot@/language/advanced-attributes.md#adv-attr-preferLocalBuild), which are always built locally.

          > **Note**
          >
          > The number of CPU cores to use for each build job is independently determined by the [`cores`](#conf-cores) setting.

          <!-- TODO(@fricklerhandwerk): would be good to have those shorthands for common options as part of the specification -->
          The setting can be overridden using the `--max-jobs` (`-j`) command line switch.
        )",
        {"build-max-jobs"}};

    Setting<unsigned int> maxSubstitutionJobs{
        this,
        16,
        "max-substitution-jobs",
        R"(
          This option defines the maximum number of substitution jobs that Nix
          tries to run in parallel. The default is `16`. The minimum value
          one can choose is `1` and lower values are interpreted as `1`.
        )",
        {"substitution-max-jobs"}};

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

    /**
     * Read-only mode.  Don't copy stuff to the store, don't change
     * the database.
     */
    bool readOnlyMode = false;

    Setting<std::string> thisSystem{
        this,
        NIX_LOCAL_SYSTEM,
        "system",
        R"(
          The system type of the current Nix installation.
          Nix only builds a given [store derivation](@docroot@/glossary.md#gloss-store-derivation) locally when its `system` attribute equals any of the values specified here or in [`extra-platforms`](#conf-extra-platforms).

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
          [`builtins.currentSystem`](@docroot@/language/builtins.md#builtins-currentSystem)
          if the
          [`eval-system`](#conf-eval-system)
          configuration option is set as the empty string.
        )"};

    Setting<time_t> maxSilentTime{
        this,
        0,
        "max-silent-time",
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
        this,
        0,
        "timeout",
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

    Setting<Strings> buildHook{
        this,
        {"nix", "__build-remote"},
        "build-hook",
        R"(
          The path to the helper program that executes remote builds.

          Nix communicates with the build hook over `stdio` using a custom protocol to request builds that cannot be performed directly by the Nix daemon.
          The default value is the internal Nix binary that implements remote building.

          > **Important**
          >
          > Change this setting only if you really know what you’re doing.
        )"};

    Setting<std::string> builders{
        this,
        "@" + nixConfDir.string() + "/machines",
        "builders",
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

          1. The URI of the remote store in the format `ssh://[username@]hostname[:port]`.

             > **Example**
             >
             > `ssh://nix@mac`

             For backward compatibility, `ssh://` may be omitted.
             The hostname may be an alias defined in `~/.ssh/config`.

          2. A comma-separated list of [Nix system types](@docroot@/development/building.md#system-type).
             If omitted, this defaults to the local platform type.

             > **Example**
             >
             > `aarch64-darwin`

             It is possible for a machine to support multiple platform types.

             > **Example**
             >
             > `i686-linux,x86_64-linux`

          3. The SSH identity file to be used to log in to the remote machine.
             If omitted, SSH uses its regular identities.

             > **Example**
             >
             > `/home/user/.ssh/id_mac`

          4. The maximum number of builds that Nix executes in parallel on the machine.
             Typically this should be equal to the number of CPU cores.

          5. The “speed factor”, indicating the relative speed of the machine as a positive integer.
             If there are multiple machines of the right type, Nix prefers the fastest, taking load into account.

          6. A comma-separated list of supported [system features](#conf-system-features).

             A machine is only used to build a derivation if all the features in the derivation's [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.html#adv-attr-requiredSystemFeatures) attribute are supported by that machine.

          7. A comma-separated list of required [system features](#conf-system-features).

             A machine is only used to build a derivation if all of the machine’s required features appear in the derivation’s [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.html#adv-attr-requiredSystemFeatures) attribute.

          8. The (base64-encoded) public host key of the remote machine.
             If omitted, SSH uses its regular `known_hosts` file.

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
          > However, `poochie` only builds derivations that have the attribute
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
          > For regular builds, `itchy` is preferred over `scratchy` because it has a higher speed factor.

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

          If you want the remote machines to use substituters, set [`builders-use-substitutes`](#conf-builders-use-substitutes) to `true`.
        )",
        {},
        false};

    Setting<bool> alwaysAllowSubstitutes{
        this,
        false,
        "always-allow-substitutes",
        R"(
          If set to `true`, Nix ignores the [`allowSubstitutes`](@docroot@/language/advanced-attributes.md) attribute in derivations and always attempt to use [available substituters](#conf-substituters).
        )"};

    Setting<bool> buildersUseSubstitutes{
        this,
        false,
        "builders-use-substitutes",
        R"(
          If set to `true`, Nix instructs [remote build machines](#conf-builders) to use their own [`substituters`](#conf-substituters) if available.

          It means that remote build hosts fetch as many dependencies as possible from their own substituters (e.g, from `cache.nixos.org`) instead of waiting for the local machine to upload them all.
          This can drastically reduce build times if the network connection between the local machine and the remote build host is slow.
        )"};

    Setting<bool> useSubstitutes{
        this,
        true,
        "substitute",
        R"(
          If set to `true` (default), Nix uses binary substitutes if
          available. This option can be disabled to force building from
          source.
        )",
        {"build-use-substitutes"}};

    Setting<unsigned long> maxLogSize{
        this,
        0,
        "max-build-log-size",
        R"(
          This option defines the maximum number of bytes that a builder can
          write to its stdout/stderr. If the builder exceeds this limit, it’s
          killed. A value of `0` (the default) means that there is no limit.
        )",
        {"build-max-log-size"}};

    Setting<unsigned int> pollInterval{this, 5, "build-poll-interval", "How often (in seconds) to poll for locks."};

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
          - the store URL is configured with `trusted=true`
          - the store object is [content-addressed](@docroot@/glossary.md#gloss-content-addressed-store-object)
        )",
        {"binary-cache-public-keys"}};

    Setting<Strings> secretKeyFiles{
        this,
        {},
        "secret-key-files",
        R"(
          A whitespace-separated list of files containing secret (private)
          keys. These are used to sign locally-built paths. They can be
          generated using `nix-store --generate-binary-cache-key`. The
          corresponding public key can be distributed to other users, who
          can add it to `trusted-public-keys` in their `nix.conf`.
        )"};

    Setting<unsigned int> tarballTtl{
        this,
        60 * 60,
        "tarball-ttl",
        R"(
          The number of seconds a downloaded tarball is considered fresh. If
          the cached tarball is stale, Nix checks whether it is still up
          to date using the ETag header. Nix downloads a new version if
          the ETag header is unsupported, or the cached ETag doesn't match.

          Setting the TTL to `0` forces Nix to always check if the tarball is
          up to date.

          Nix caches tarballs in `$XDG_CACHE_HOME/nix/tarballs`.

          Files fetched via `NIX_PATH`, `fetchGit`, `fetchMercurial`,
          `fetchTarball`, and `fetchurl` respect this TTL.
        )"};

    Setting<bool> requireSigs{
        this,
        true,
        "require-sigs",
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

          Nix only builds a given [store derivation](@docroot@/glossary.md#gloss-store-derivation) locally when its `system` attribute equals any of the values specified here or in the [`system` option](#conf-system).

          Setting this can be useful to build derivations locally on compatible machines:
          - `i686-linux` executables can be run on `x86_64-linux` machines (set by default)
          - `x86_64-darwin` executables can be run on macOS `aarch64-darwin` with Rosetta 2 (set by default where applicable)
          - `armv6` and `armv5tel` executables can be run on `armv7`
          - some `aarch64` machines can also natively run 32-bit ARM code
          - `qemu-user` may be used to support non-native platforms (though this
          may be slow and buggy)

          Build systems usually detect the target platform to be the current physical system and therefore produce machine code incompatible with what may be intended in the derivation.
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

            Included by default if the [`ca-derivations` experimental feature](@docroot@/development/experimental-features.md#xp-feature-ca-derivations) is enabled.

            This system feature is implicitly required by derivations with the [`__contentAddressed` attribute](@docroot@/language/advanced-attributes.md#adv-attr-__contentAddressed).

          - `recursive-nix`

            Included by default if the [`recursive-nix` experimental feature](@docroot@/development/experimental-features.md#xp-feature-recursive-nix) is enabled.

          - `uid-range`

            On Linux, Nix can run builds in a user namespace where they run as root (UID 0) and have 65,536 UIDs available.
            This is primarily useful for running containers such as `systemd-nspawn` inside a Nix build. For an example, see [`tests/systemd-nspawn/nix`][nspawn].

            [nspawn]: https://github.com/NixOS/nix/blob/67bcb99700a0da1395fa063d7c6586740b304598/tests/systemd-nspawn.nix

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
          A substituter is an additional [store](@docroot@/glossary.md#gloss-store) from which Nix can obtain [store objects](@docroot@/store/store-object.md) instead of building them.

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
        this,
        {},
        "trusted-substituters",
        R"(
          A list of [Nix store URLs](@docroot@/store/types/index.md#store-url-format), separated by whitespace.
          These are not used by default, but users of the Nix daemon can enable them by specifying [`substituters`](#conf-substituters).

          Unprivileged users (those set in only [`allowed-users`](#conf-allowed-users) but not [`trusted-users`](#conf-trusted-users)) can pass as `substituters` only those URLs listed in `trusted-substituters`.
        )",
        {"trusted-binary-caches"}};

    Setting<unsigned int> ttlNegativeNarInfoCache{
        this,
        3600,
        "narinfo-cache-negative-ttl",
        R"(
          The TTL in seconds for negative lookups.
          If a store path is queried from a [substituter](#conf-substituters) but was not found, a negative lookup is cached in the local disk cache database for the specified duration.

          Set to `0` to force updating the lookup cache.

          To wipe the lookup cache completely:

          ```shell-session
          $ rm $HOME/.cache/nix/binary-cache-v*.sqlite*
          # rm /root/.cache/nix/binary-cache-v*.sqlite*
          ```
        )"};

    Setting<unsigned int> ttlPositiveNarInfoCache{
        this,
        30 * 24 * 3600,
        "narinfo-cache-positive-ttl",
        R"(
          The TTL in seconds for positive lookups. If a store path is queried
          from a substituter, the result of the query is cached in the
          local disk cache database including some of the NAR metadata. The
          default TTL is a month, setting a shorter TTL for positive lookups
          can be useful for binary caches that have frequent garbage
          collection, in which case having a more frequent cache invalidation
          would prevent trying to pull the path again and failing with a hash
          mismatch if the build isn't reproducible.
        )"};

    Setting<bool> printMissing{
        this, true, "print-missing", "Whether to print what paths need to be built or downloaded."};

    Setting<std::string> postBuildHook{
        this,
        "",
        "post-build-hook",
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
              `/nix/store/l88brggg9hpy96ijds34dlq4n8fan63g-bash-4.4-p23-dev
              /nix/store/vch71bhyi5akr5zs40k8h2wqxx69j80l-bash-4.4-p23-doc
              /nix/store/c5cxjywi66iwn9dcx5yvwjkvl559ay6p-bash-4.4-p23-info
              /nix/store/scz72lskj03ihkcn42ias5mlp4i4gr1k-bash-4.4-p23-man
              /nix/store/a724znygmd1cac856j3gfsyvih3lw07j-bash-4.4-p23`.
        )"};

    Setting<unsigned int> downloadSpeed{
        this,
        0,
        "download-speed",
        R"(
          Specify the maximum transfer rate in kilobytes per second you want
          Nix to use for downloads.
        )"};

    Setting<std::string> netrcFile{
        this,
        (nixConfDir / "netrc").string(),
        "netrc-file",
        R"(
          If set to an absolute path to a `netrc` file, Nix uses the HTTP
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
        this,
        getDefaultSSLCertFile(),
        "ssl-cert-file",
        R"(
          The path of a file containing CA certificates used to
          authenticate `https://` downloads. Nix by default uses
          the first of the following files that exists:

          1. `/etc/ssl/certs/ca-certificates.crt`
          2. `/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt`

          The path can be overridden by the following environment
          variables, in order of precedence:

          1. `NIX_SSL_CERT_FILE`
          2. `SSL_CERT_FILE`
        )",
        {},
        // Don't document the machine-specific default value
        false};

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

    Setting<bool> useXDGBaseDirectories{
        this,
        false,
        "use-xdg-base-directories",
        R"(
          If set to `true`, Nix conforms to the [XDG Base Directory Specification] for files in `$HOME`.
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
        )"};

    Setting<uint64_t> warnLargePathThreshold{
        this,
        0,
        "warn-large-path-threshold",
        R"(
          Warn when copying a path larger than this number of bytes to the Nix store
          (as determined by its NAR serialisation).
          Default is 0, which disables the warning.
          Set it to 1 to warn on all paths.
        )"};

    /**
     * Finds the first external derivation builder that supports this
     * derivation, or else returns a null pointer.
     */
    const ExternalBuilder * findExternalDerivationBuilderIfSupported(const Derivation & drv);
};

// FIXME: don't use a global variable.
extern Settings settings;

/**
 * Load the configuration (from `nix.conf`, `NIX_CONFIG`, etc.) into the
 * given configuration object.
 *
 * Usually called with `globalConfig`.
 */
void loadConfFile(AbstractConfig & config);

// Used by the Settings constructor
std::vector<Path> getUserConfigFiles();

/**
 * The version of Nix itself.
 *
 * This is not `const`, so that the Nix CLI can provide a more detailed version
 * number including the git revision, without having to "re-compile" the entire
 * set of Nix libraries to include that version, even when those libraries are
 * not affected by the change.
 */
extern std::string nixVersion;

/**
 * @param loadConfig Whether to load configuration from `nix.conf`, `NIX_CONFIG`, etc. May be disabled for unit tests.
 * @note When using libexpr, and/or libmain, This is not sufficient. See initNix().
 */
void initLibStore(bool loadConfig = true);

/**
 * It's important to initialize before doing _anything_, which is why we
 * call upon the programmer to handle this correctly. However, we only add
 * this in a key locations, so as not to litter the code.
 */
void assertLibStoreInitialized();

} // namespace nix

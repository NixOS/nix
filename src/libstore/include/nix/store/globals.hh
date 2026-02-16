#pragma once
///@file

#include <sys/types.h>

#include "nix/util/types.hh"
#include "nix/util/configuration.hh"
#include "nix/util/environment-variables.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/store/local-settings.hh"
#include "nix/store/store-reference.hh"
#include "nix/store/worker-settings.hh"

#include "nix/store/config.hh"

namespace nix {

struct ProfileDirsOptions;

struct LogFileSettings : public virtual Config
{
    /**
     * The directory where we log various operations.
     */
    std::filesystem::path nixLogDir;

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

    Setting<bool> compressLog{
        this,
        true,
        "compress-build-log",
        R"(
          If set to `true` (the default), build logs written to
          `/nix/var/log/nix/drvs` are compressed on the fly using bzip2.
          Otherwise, they are not compressed.
        )",
        {"build-compress-log"}};
};

struct NarInfoDiskCacheSettings : public virtual Config
{
    Setting<unsigned int> ttlNegative{
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

    Setting<unsigned int> ttlPositive{
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

    Setting<unsigned int> ttlMeta{
        this,
        7 * 24 * 3600,
        "narinfo-cache-meta-ttl",
        R"(
          The TTL in seconds for caching binary cache metadata (i.e.
          `/nix-cache-info`). This determines how long information about a
          binary cache (such as its store directory, priority, and whether it
          wants mass queries) is considered valid before being refreshed.
        )"};
};

class Settings : public virtual Config,
                 private LocalSettings,
                 private LogFileSettings,
                 private WorkerSettings,
                 private NarInfoDiskCacheSettings
{
    StringSet getDefaultSystemFeatures();

    StringSet getDefaultExtraPlatforms();

    bool isWSL1();

public:

    Settings();

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

    /**
     * Get the worker settings.
     */
    WorkerSettings & getWorkerSettings()
    {
        return *this;
    }

    const WorkerSettings & getWorkerSettings() const
    {
        return *this;
    }

    /**
     * Get the NAR info disk cache settings.
     */
    NarInfoDiskCacheSettings & getNarInfoDiskCacheSettings()
    {
        return *this;
    }

    const NarInfoDiskCacheSettings & getNarInfoDiskCacheSettings() const
    {
        return *this;
    }

    static unsigned int getDefaultCores();

    /**
     * The directory where state is stored.
     */
    std::filesystem::path nixStateDir;

    /**
     * File name of the socket the daemon listens to.
     */
    std::filesystem::path nixDaemonSocketFile;

    Setting<StoreReference> storeUri{
        this,
        StoreReference::parse(getEnv("NIX_REMOTE").value_or("auto")),
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

    /**
     * Whether to show build log output in real time.
     */
    bool verboseBuild = true;

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

    // move to daemonsettings in another pass
    //
    // we'll add a another parameter to processConnection to thread it through
    Setting<std::set<StoreReference>> trustedSubstituters{
        this,
        {},
        "trusted-substituters",
        R"(
          A list of [Nix store URLs](@docroot@/store/types/index.md#store-url-format), separated by whitespace.
          These are not used by default, but users of the Nix daemon can enable them by specifying [`substituters`](#conf-substituters).

          Unprivileged users (those set in only [`allowed-users`](#conf-allowed-users) but not [`trusted-users`](#conf-trusted-users)) can pass as `substituters` only those URLs listed in `trusted-substituters`.
        )",
        {"trusted-binary-caches"}};

    // move it out in the 2nd pass
    Setting<bool> printMissing{
        this, true, "print-missing", "Whether to print what paths need to be built or downloaded."};

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
     * Get the options needed for profile directory functions.
     */
    ProfileDirsOptions getProfileDirsOptions() const;
};

// FIXME: don't use a global variable.
extern nix::Settings settings;

/**
 * Load the configuration (from `nix.conf`, `NIX_CONFIG`, etc.) into the
 * given configuration object.
 *
 * Usually called with `globalConfig`.
 */
void loadConfFile(AbstractConfig & config);

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

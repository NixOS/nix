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

    /* The directory where configuration files are stored. */
    Path nixConfDir;

    /* The directory where internal helper programs are stored. */
    Path nixLibexecDir;

    /* The directory where the main programs are stored. */
    Path nixBinDir;

    /* The directory where the man pages are stored. */
    Path nixManDir;

    /* File name of the socket the daemon listens to.  */
    Path nixDaemonSocketFile;

    Setting<std::string> storeUri{this, getEnv("NIX_REMOTE", "auto"), "store",
        "The default Nix store to use."};

    Setting<bool> keepFailed{this, false, "keep-failed",
        "Whether to keep temporary directories of failed builds."};

    Setting<bool> keepGoing{this, false, "keep-going",
        "Whether to keep building derivations when another build fails."};

    Setting<bool> tryFallback{this, false, "fallback",
        "Whether to fall back to building when substitution fails.",
        {"build-fallback"}};

    /* Whether to show build log output in real time. */
    bool verboseBuild = true;

    Setting<size_t> logLines{this, 10, "log-lines",
        "If verbose-build is false, the number of lines of the tail of "
        "the log to show if a build fails."};

    MaxBuildJobsSetting maxBuildJobs{this, 1, "max-jobs",
        "Maximum number of parallel build jobs. \"auto\" means use number of cores.",
        {"build-max-jobs"}};

    Setting<unsigned int> buildCores{this, getDefaultCores(), "cores",
        "Number of CPU cores to utilize in parallel within a build, "
        "i.e. by passing this number to Make via '-j'. 0 means that the "
        "number of actual CPU cores on the local host ought to be "
        "auto-detected.", {"build-cores"}};

    /* Read-only mode.  Don't copy stuff to the store, don't change
       the database. */
    bool readOnlyMode = false;

    Setting<std::string> thisSystem{this, SYSTEM, "system",
        "The canonical Nix system name."};

    Setting<time_t> maxSilentTime{this, 0, "max-silent-time",
        "The maximum time in seconds that a builer can go without "
        "producing any output on stdout/stderr before it is killed. "
        "0 means infinity.",
        {"build-max-silent-time"}};

    Setting<time_t> buildTimeout{this, 0, "timeout",
        "The maximum duration in seconds that a builder can run. "
        "0 means infinity.", {"build-timeout"}};

    PathSetting buildHook{this, true, nixLibexecDir + "/nix/build-remote", "build-hook",
        "The path of the helper program that executes builds to remote machines."};

    Setting<std::string> builders{this, "@" + nixConfDir + "/machines", "builders",
        "A semicolon-separated list of build machines, in the format of nix.machines."};

    Setting<bool> buildersUseSubstitutes{this, false, "builders-use-substitutes",
        "Whether build machines should use their own substitutes for obtaining "
        "build dependencies if possible, rather than waiting for this host to "
        "upload them."};

    Setting<off_t> reservedSize{this, 8 * 1024 * 1024, "gc-reserved-space",
        "Amount of reserved disk space for the garbage collector."};

    Setting<bool> fsyncMetadata{this, true, "fsync-metadata",
        "Whether SQLite should use fsync()."};

    Setting<bool> useSQLiteWAL{this, true, "use-sqlite-wal",
        "Whether SQLite should use WAL mode."};

    Setting<bool> syncBeforeRegistering{this, false, "sync-before-registering",
        "Whether to call sync() before registering a path as valid."};

    Setting<bool> useSubstitutes{this, true, "substitute",
        "Whether to use substitutes.",
        {"build-use-substitutes"}};

    Setting<std::string> buildUsersGroup{this, "", "build-users-group",
        "The Unix group that contains the build users."};

    Setting<bool> impersonateLinux26{this, false, "impersonate-linux-26",
        "Whether to impersonate a Linux 2.6 machine on newer kernels.",
        {"build-impersonate-linux-26"}};

    Setting<bool> keepLog{this, true, "keep-build-log",
        "Whether to store build logs.",
        {"build-keep-log"}};

    Setting<bool> compressLog{this, true, "compress-build-log",
        "Whether to compress logs.",
        {"build-compress-log"}};

    Setting<unsigned long> maxLogSize{this, 0, "max-build-log-size",
        "Maximum number of bytes a builder can write to stdout/stderr "
        "before being killed (0 means no limit).",
        {"build-max-log-size"}};

    /* When buildRepeat > 0 and verboseBuild == true, whether to print
       repeated builds (i.e. builds other than the first one) to
       stderr. Hack to prevent Hydra logs from being polluted. */
    bool printRepeatedBuilds = true;

    Setting<unsigned int> pollInterval{this, 5, "build-poll-interval",
        "How often (in seconds) to poll for locks."};

    Setting<bool> checkRootReachability{this, false, "gc-check-reachability",
        "Whether to check if new GC roots can in fact be found by the "
        "garbage collector."};

    Setting<bool> gcKeepOutputs{this, false, "keep-outputs",
        "Whether the garbage collector should keep outputs of live derivations.",
        {"gc-keep-outputs"}};

    Setting<bool> gcKeepDerivations{this, true, "keep-derivations",
        "Whether the garbage collector should keep derivers of live paths.",
        {"gc-keep-derivations"}};

    Setting<bool> autoOptimiseStore{this, false, "auto-optimise-store",
        "Whether to automatically replace files with identical contents with hard links."};

    Setting<bool> envKeepDerivations{this, false, "keep-env-derivations",
        "Whether to add derivations as a dependency of user environments "
        "(to prevent them from being GCed).",
        {"env-keep-derivations"}};

    /* Whether to lock the Nix client and worker to the same CPU. */
    bool lockCPU;

    /* Whether to show a stack trace if Nix evaluation fails. */
    Setting<bool> showTrace{this, false, "show-trace",
        "Whether to show a stack trace on evaluation errors."};

    Setting<SandboxMode> sandboxMode{this,
        #if __linux__
          smEnabled
        #else
          smDisabled
        #endif
        , "sandbox",
        "Whether to enable sandboxed builds. Can be \"true\", \"false\" or \"relaxed\".",
        {"build-use-chroot", "build-use-sandbox"}};

    Setting<PathSet> sandboxPaths{this, {}, "sandbox-paths",
        "The paths to make available inside the build sandbox.",
        {"build-chroot-dirs", "build-sandbox-paths"}};

    Setting<bool> sandboxFallback{this, true, "sandbox-fallback",
        "Whether to disable sandboxing when the kernel doesn't allow it."};

    Setting<PathSet> extraSandboxPaths{this, {}, "extra-sandbox-paths",
        "Additional paths to make available inside the build sandbox.",
        {"build-extra-chroot-dirs", "build-extra-sandbox-paths"}};

    Setting<size_t> buildRepeat{this, 0, "repeat",
        "The number of times to repeat a build in order to verify determinism.",
        {"build-repeat"}};

#if __linux__
    Setting<std::string> sandboxShmSize{this, "50%", "sandbox-dev-shm-size",
        "The size of /dev/shm in the build sandbox."};

    Setting<Path> sandboxBuildDir{this, "/build", "sandbox-build-dir",
        "The build directory inside the sandbox."};
#endif

    Setting<PathSet> allowedImpureHostPrefixes{this, {}, "allowed-impure-host-deps",
        "Which prefixes to allow derivations to ask for access to (primarily for Darwin)."};

#if __APPLE__
    Setting<bool> darwinLogSandboxViolations{this, false, "darwin-log-sandbox-violations",
        "Whether to log Darwin sandbox access violations to the system log."};
#endif

    Setting<bool> runDiffHook{this, false, "run-diff-hook",
        "Whether to run the program specified by the diff-hook setting "
        "repeated builds produce a different result. Typically used to "
        "plug in diffoscope."};

    PathSetting diffHook{this, true, "", "diff-hook",
        "A program that prints out the differences between the two paths "
        "specified on its command line."};

    Setting<bool> enforceDeterminism{this, true, "enforce-determinism",
        "Whether to fail if repeated builds produce different output."};

    Setting<Strings> trustedPublicKeys{this,
        {"cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="},
        "trusted-public-keys",
        "Trusted public keys for secure substitution.",
        {"binary-cache-public-keys"}};

    Setting<Strings> secretKeyFiles{this, {}, "secret-key-files",
        "Secret keys with which to sign local builds."};

    Setting<unsigned int> tarballTtl{this, 60 * 60, "tarball-ttl",
        "How long downloaded files are considered up-to-date."};

    Setting<bool> requireSigs{this, true, "require-sigs",
        "Whether to check that any non-content-addressed path added to the "
        "Nix store has a valid signature (that is, one signed using a key "
        "listed in 'trusted-public-keys'."};

    Setting<StringSet> extraPlatforms{this,
        std::string{SYSTEM} == "x86_64-linux" ? StringSet{"i686-linux"} : StringSet{},
        "extra-platforms",
        "Additional platforms that can be built on the local system. "
        "These may be supported natively (e.g. armv7 on some aarch64 CPUs "
        "or using hacks like qemu-user."};

    Setting<StringSet> systemFeatures{this, getDefaultSystemFeatures(),
        "system-features",
        "Optional features that this system implements (like \"kvm\")."};

    Setting<Strings> substituters{this,
        nixStore == "/nix/store" ? Strings{"https://cache.nixos.org/"} : Strings(),
        "substituters",
        "The URIs of substituters (such as https://cache.nixos.org/).",
        {"binary-caches"}};

    // FIXME: provide a way to add to option values.
    Setting<Strings> extraSubstituters{this, {}, "extra-substituters",
        "Additional URIs of substituters.",
        {"extra-binary-caches"}};

    Setting<StringSet> trustedSubstituters{this, {}, "trusted-substituters",
        "Disabled substituters that may be enabled via the substituters option by untrusted users.",
        {"trusted-binary-caches"}};

    Setting<Strings> trustedUsers{this, {"root"}, "trusted-users",
        "Which users or groups are trusted to ask the daemon to do unsafe things."};

    Setting<unsigned int> ttlNegativeNarInfoCache{this, 3600, "narinfo-cache-negative-ttl",
        "The TTL in seconds for negative lookups in the disk cache i.e binary cache lookups that "
        "return an invalid path result"};

    Setting<unsigned int> ttlPositiveNarInfoCache{this, 30 * 24 * 3600, "narinfo-cache-positive-ttl",
        "The TTL in seconds for positive lookups in the disk cache i.e binary cache lookups that "
        "return a valid path result."};

    /* ?Who we trust to use the daemon in safe ways */
    Setting<Strings> allowedUsers{this, {"*"}, "allowed-users",
        "Which users or groups are allowed to connect to the daemon."};

    Setting<bool> printMissing{this, true, "print-missing",
        "Whether to print what paths need to be built or downloaded."};

    Setting<std::string> preBuildHook{this,
#if __APPLE__
        nixLibexecDir + "/nix/resolve-system-dependencies",
#else
        "",
#endif
        "pre-build-hook",
        "A program to run just before a build to set derivation-specific build settings."};

    Setting<std::string> postBuildHook{this, "", "post-build-hook",
        "A program to run just after each successful build."};

    Setting<std::string> netrcFile{this, fmt("%s/%s", nixConfDir, "netrc"), "netrc-file",
        "Path to the netrc file used to obtain usernames/passwords for downloads."};

    /* Path to the SSL CA file used */
    Path caFile;

#if __linux__
    Setting<bool> filterSyscalls{this, true, "filter-syscalls",
            "Whether to prevent certain dangerous system calls, such as "
            "creation of setuid/setgid files or adding ACLs or extended "
            "attributes. Only disable this if you're aware of the "
            "security implications."};

    Setting<bool> allowNewPrivileges{this, false, "allow-new-privileges",
        "Whether builders can acquire new privileges by calling programs with "
        "setuid/setgid bits or with file capabilities."};
#endif

    Setting<Strings> hashedMirrors{this, {"http://tarballs.nixos.org/"}, "hashed-mirrors",
        "A list of servers used by builtins.fetchurl to fetch files by hash."};

    Setting<uint64_t> minFree{this, 0, "min-free",
        "Automatically run the garbage collector when free disk space drops below the specified amount."};

    Setting<uint64_t> maxFree{this, std::numeric_limits<uint64_t>::max(), "max-free",
        "Stop deleting garbage when free disk space is above the specified amount."};

    Setting<uint64_t> minFreeCheckInterval{this, 5, "min-free-check-interval",
        "Number of seconds between checking free disk space."};

    Setting<Paths> pluginFiles{this, {}, "plugin-files",
        "Plugins to dynamically load at nix initialization time."};
};


// FIXME: don't use a global variable.
extern Settings settings;

/* This should be called after settings are initialized, but before
   anything else */
void initPlugins();

void loadConfFile();

extern const string nixVersion;

}

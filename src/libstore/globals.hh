#pragma once

#include "types.hh"
#include "config.hh"

#include <map>
#include <sys/types.h>


namespace nix {

typedef enum { smEnabled, smRelaxed, smDisabled } SandboxMode;

extern bool useCaseHack; // FIXME

struct CaseHackSetting : public BaseSetting<bool>
{
    CaseHackSetting(Config * options,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : BaseSetting<bool>(useCaseHack, name, description, aliases)
    {
        options->addSetting(this);
    }

    void set(const std::string & str) override
    {
        BaseSetting<bool>::set(str);
        nix::useCaseHack = true;
    }
};

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

public:

    Settings();

    void loadConfFile();

    void set(const string & name, const string & value);

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

    /* File name of the socket the daemon listens to.  */
    Path nixDaemonSocketFile;

    Setting<bool> keepFailed{this, false, "keep-failed",
        "Whether to keep temporary directories of failed builds."};

    Setting<bool> keepGoing{this, false, "keep-going",
        "Whether to keep building derivations when another build fails."};

    Setting<bool> tryFallback{this, false, "fallback",
        "Whether to fall back to building when substitution fails.",
        {"build-fallback"}};

    /* Whether to show build log output in real time. */
    bool verboseBuild = true;

    /* If verboseBuild is false, the number of lines of the tail of
       the log to show if a build fails. */
    size_t logLines = 10;

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

    Setting<bool> useBuildHook{this, true, "remote-builds",
        "Whether to use build hooks (for distributed builds)."};

    PathSetting buildHook{this, true, nixLibexecDir + "/nix/build-remote", "build-hook",
        "The path of the helper program that executes builds to remote machines."};

    Setting<std::string> builders{this, "", "builders",
        "A semicolon-separated list of build machines, in the format of nix.machines."};

    Setting<Strings> builderFiles{this,
        {nixConfDir + "/machines"}, "builder-files",
        "A list of files specifying build machines."};

    Setting<off_t> reservedSize{this, 8 * 1024 * 1024, "gc-reserved-space",
        "Amount of reserved disk space for the garbage collector."};

    Setting<bool> fsyncMetadata{this, true, "fsync-metadata",
        "Whether SQLite should use fsync()."};

    Setting<bool> useSQLiteWAL{this, true, "use-sqlite-wal",
        "Whether SQLite should use WAL mode."};

    Setting<bool> syncBeforeRegistering{this, false, "sync-before-registering",
        "Whether to call sync() before registering a path as valid."};

    Setting<bool> useSubstitutes{this, true, "use-substitutes",
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
    bool showTrace = false;

    Setting<bool> enableNativeCode{this, false, "allow-unsafe-native-code-during-evaluation",
        "Whether builtin functions that allow executing native code should be enabled."};

    Setting<SandboxMode> sandboxMode{this, smDisabled, "sandbox",
        "Whether to enable sandboxed builds. Can be \"true\", \"false\" or \"relaxed\".",
        {"build-use-chroot", "build-use-sandbox"}};

    Setting<PathSet> sandboxPaths{this, {}, "sandbox-paths",
        "The paths to make available inside the build sandbox.",
        {"build-chroot-dirs", "build-sandbox-paths"}};

    Setting<PathSet> extraSandboxPaths{this, {}, "extra-sandbox-paths",
        "Additional paths to make available inside the build sandbox.",
        {"build-extra-chroot-dirs", "build-extra-sandbox-paths"}};

    Setting<bool> restrictEval{this, false, "restrict-eval",
        "Whether to restrict file system access to paths in $NIX_PATH, "
        "and to disallow fetching files from the network."};

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

    Setting<Strings> binaryCachePublicKeys{this,
        {"cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="},
        "binary-cache-public-keys",
        "Trusted public keys for secure substitution."};

    Setting<Strings> secretKeyFiles{this, {}, "secret-key-files",
        "Secret keys with which to sign local builds."};

    Setting<size_t> binaryCachesParallelConnections{this, 25, "http-connections",
        "Number of parallel HTTP connections.",
        {"binary-caches-parallel-connections"}};

    Setting<bool> enableHttp2{this, true, "enable-http2",
        "Whether to enable HTTP/2 support."};

    Setting<unsigned int> tarballTtl{this, 60 * 60, "tarball-ttl",
        "How soon to expire files fetched by builtins.fetchTarball and builtins.fetchurl."};

    Setting<std::string> signedBinaryCaches{this, "*", "signed-binary-caches",
        "Obsolete."};

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

    Setting<std::string> netrcFile{this, fmt("%s/%s", nixConfDir, "netrc"), "netrc-file",
        "Path to the netrc file used to obtain usernames/passwords for downloads."};

    /* Path to the SSL CA file used */
    Path caFile;

    Setting<bool> enableImportFromDerivation{this, true, "allow-import-from-derivation",
        "Whether the evaluator allows importing the result of a derivation."};

    CaseHackSetting useCaseHack{this, "use-case-hack",
        "Whether to enable a Darwin-specific hack for dealing with file name collisions."};

    Setting<unsigned long> connectTimeout{this, 0, "connect-timeout",
        "Timeout for connecting to servers during downloads. 0 means use curl's builtin default."};

    Setting<std::string> userAgentSuffix{this, "", "user-agent-suffix",
        "String appended to the user agent in HTTP requests."};

#if __linux__
    Setting<bool> allowNewPrivileges{this, false, "allow-new-privileges",
        "Whether builders can acquire new privileges by calling programs with "
        "setuid/setgid bits or with file capabilities."};
#endif

    Setting<Strings> hashedMirrors{this, {"http://tarballs.nixos.org/"}, "hashed-mirrors",
        "A list of servers used by builtins.fetchurl to fetch files by hash."};
};


// FIXME: don't use a global variable.
extern Settings settings;


extern const string nixVersion;


}

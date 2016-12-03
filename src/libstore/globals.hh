#pragma once

#include "types.hh"
#include "logging.hh"

#include <map>
#include <sys/types.h>


namespace nix {


struct Settings {

    typedef std::map<string, string> SettingsMap;

    Settings();

    void processEnvironment();

    void loadConfFile();

    void set(const string & name, const string & value);

    string get(const string & name, const string & def);

    Strings get(const string & name, const Strings & def);

    bool get(const string & name, bool def);

    int get(const string & name, int def);

    void update();

    string pack();

    void unpack(const string & pack);

    SettingsMap getOverrides();

    /* The directory where we store sources and derived files. */
    Path nixStore;

    Path nixDataDir; /* !!! fix */

    Path nixPrefix;

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

    /* Whether to keep temporary directories of failed builds. */
    bool keepFailed;

    /* Whether to keep building subgoals when a sibling (another
       subgoal of the same goal) fails. */
    bool keepGoing;

    /* Whether, if we cannot realise the known closure corresponding
       to a derivation, we should try to normalise the derivation
       instead. */
    bool tryFallback;

    /* Whether to show build log output in real time. */
    bool verboseBuild = true;

    /* If verboseBuild is false, the number of lines of the tail of
       the log to show if a build fails. */
    size_t logLines = 10;

    /* Maximum number of parallel build jobs.  0 means unlimited. */
    unsigned int maxBuildJobs;

    /* Number of CPU cores to utilize in parallel within a build,
       i.e. by passing this number to Make via '-j'. 0 means that the
       number of actual CPU cores on the local host ought to be
       auto-detected. */
    unsigned int buildCores;

    /* Read-only mode.  Don't copy stuff to the store, don't change
       the database. */
    bool readOnlyMode;

    /* The canonical system name, as returned by config.guess. */
    string thisSystem;

    /* The maximum time in seconds that a builer can go without
       producing any output on stdout/stderr before it is killed.  0
       means infinity. */
    time_t maxSilentTime;

    /* The maximum duration in seconds that a builder can run.  0
       means infinity.  */
    time_t buildTimeout;

    /* Whether to use build hooks (for distributed builds).  Sometimes
       users want to disable this from the command-line. */
    bool useBuildHook;

    /* Amount of reserved space for the garbage collector
       (/nix/var/nix/db/reserved). */
    off_t reservedSize;

    /* Whether SQLite should use fsync. */
    bool fsyncMetadata;

    /* Whether SQLite should use WAL mode. */
    bool useSQLiteWAL;

    /* Whether to call sync() before registering a path as valid. */
    bool syncBeforeRegistering;

    /* Whether to use substitutes. */
    bool useSubstitutes;

    /* The Unix group that contains the build users. */
    string buildUsersGroup;

    /* Set of ssh connection strings for the ssh substituter */
    Strings sshSubstituterHosts;

    /* Whether to use the ssh substituter at all */
    bool useSshSubstituter;

    /* Whether to impersonate a Linux 2.6 machine on newer kernels. */
    bool impersonateLinux26;

    /* Whether to store build logs. */
    bool keepLog;

    /* Whether to compress logs. */
    bool compressLog;

    /* Maximum number of bytes a builder can write to stdout/stderr
       before being killed (0 means no limit). */
    unsigned long maxLogSize;

    /* When build-repeat > 0 and verboseBuild == true, whether to
       print repeated builds (i.e. builds other than the first one) to
       stderr. Hack to prevent Hydra logs from being polluted. */
    bool printRepeatedBuilds = true;

    /* How often (in seconds) to poll for locks. */
    unsigned int pollInterval;

    /* Whether to check if new GC roots can in fact be found by the
       garbage collector. */
    bool checkRootReachability;

    /* Whether the garbage collector should keep outputs of live
       derivations. */
    bool gcKeepOutputs;

    /* Whether the garbage collector should keep derivers of live
       paths. */
    bool gcKeepDerivations;

    /* Whether to automatically replace files with identical contents
       with hard links. */
    bool autoOptimiseStore;

    /* Whether to add derivations as a dependency of user environments
       (to prevent them from being GCed). */
    bool envKeepDerivations;

    /* Whether to lock the Nix client and worker to the same CPU. */
    bool lockCPU;

    /* Whether to show a stack trace if Nix evaluation fails. */
    bool showTrace;

    /* A list of URL prefixes that can return Nix build logs. */
    Strings logServers;

    /* Whether the importNative primop should be enabled */
    bool enableImportNative;

    /* The hook to run just before a build to set derivation-specific
       build settings */
    Path preBuildHook;

    /* Host where a IPFS API can be reached (usually localhost) */
    std::string ipfsAPIHost;
    /* Port where a IPFS API can be reached (usually 5001) */
    uint16_t    ipfsAPIPort;
    /* Whether to use a IPFS Gateway instead of the API */
    bool        useIpfsGateway;
    /* Where to find a IPFS Gateway */
    std::string ipfsGatewayURL;

private:
    SettingsMap settings, overrides;

    void _get(string & res, const string & name);
    void _get(bool & res, const string & name);
    void _get(StringSet & res, const string & name);
    void _get(Strings & res, const string & name);
    template<class N> void _get(N & res, const string & name);
};


// FIXME: don't use a global variable.
extern Settings settings;


extern const string nixVersion;


}

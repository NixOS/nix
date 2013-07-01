#include "config.h"

#include "globals.hh"
#include "util.hh"

#include <map>
#include <algorithm>


namespace nix {


/* The default location of the daemon socket, relative to nixStateDir.
   The socket is in a directory to allow you to control access to the
   Nix daemon by setting the mode/ownership of the directory
   appropriately.  (This wouldn't work on the socket itself since it
   must be deleted and recreated on startup.) */
#define DEFAULT_SOCKET_PATH "/daemon-socket/socket"


Settings settings;


Settings::Settings()
{
    keepFailed = false;
    keepGoing = false;
    tryFallback = false;
    buildVerbosity = lvlError;
    maxBuildJobs = 1;
    buildCores = 1;
    readOnlyMode = false;
    thisSystem = SYSTEM;
    maxSilentTime = 0;
    buildTimeout = 0;
    useBuildHook = true;
    printBuildTrace = false;
    reservedSize = 1024 * 1024;
    fsyncMetadata = true;
    useSQLiteWAL = true;
    syncBeforeRegistering = false;
    useSubstitutes = true;
    useChroot = false;
    dirsInChroot.insert("/dev");
    dirsInChroot.insert("/dev/pts");
    impersonateLinux26 = false;
    keepLog = true;
    compressLog = true;
    cacheFailure = false;
    pollInterval = 5;
    checkRootReachability = false;
    gcKeepOutputs = false;
    gcKeepDerivations = true;
    autoOptimiseStore = false;
    envKeepDerivations = false;
    daemonUseCgroups = false;
}


void Settings::processEnvironment()
{
    nixStore = canonPath(getEnv("NIX_STORE_DIR", getEnv("NIX_STORE", NIX_STORE_DIR)));
    nixDataDir = canonPath(getEnv("NIX_DATA_DIR", NIX_DATA_DIR));
    nixLogDir = canonPath(getEnv("NIX_LOG_DIR", NIX_LOG_DIR));
    nixStateDir = canonPath(getEnv("NIX_STATE_DIR", NIX_STATE_DIR));
    nixDBPath = getEnv("NIX_DB_DIR", nixStateDir + "/db");
    nixConfDir = canonPath(getEnv("NIX_CONF_DIR", NIX_CONF_DIR));
    nixLibexecDir = canonPath(getEnv("NIX_LIBEXEC_DIR", NIX_LIBEXEC_DIR));
    nixBinDir = canonPath(getEnv("NIX_BIN_DIR", NIX_BIN_DIR));
    nixDaemonSocketFile = canonPath(nixStateDir + DEFAULT_SOCKET_PATH);

    string subs = getEnv("NIX_SUBSTITUTERS", "default");
    if (subs == "default") {
#if 0
        if (getEnv("NIX_OTHER_STORES") != "")
            substituters.push_back(nixLibexecDir + "/nix/substituters/copy-from-other-stores.pl");
#endif
        substituters.push_back(nixLibexecDir + "/nix/substituters/download-using-manifests.pl");
        substituters.push_back(nixLibexecDir + "/nix/substituters/download-from-binary-cache.pl");
    } else
        substituters = tokenizeString<Strings>(subs, ":");
}


void Settings::loadConfFile()
{
    Path settingsFile = (format("%1%/%2%") % nixConfDir % "nix.conf").str();
    if (!pathExists(settingsFile)) return;
    string contents = readFile(settingsFile);

    unsigned int pos = 0;

    while (pos < contents.size()) {
        string line;
        while (pos < contents.size() && contents[pos] != '\n')
            line += contents[pos++];
        pos++;

        string::size_type hash = line.find('#');
        if (hash != string::npos)
            line = string(line, 0, hash);

        vector<string> tokens = tokenizeString<vector<string> >(line);
        if (tokens.empty()) continue;

        if (tokens.size() < 2 || tokens[1] != "=")
            throw Error(format("illegal configuration line `%1%' in `%2%'") % line % settingsFile);

        string name = tokens[0];

        vector<string>::iterator i = tokens.begin();
        advance(i, 2);
        settings[name] = concatStringsSep(" ", Strings(i, tokens.end())); // FIXME: slow
    };
}


void Settings::set(const string & name, const string & value)
{
    settings[name] = value;
    overrides[name] = value;
}


void Settings::update()
{
    get(tryFallback, "build-fallback");
    get(maxBuildJobs, "build-max-jobs");
    get(buildCores, "build-cores");
    get(thisSystem, "system");
    get(maxSilentTime, "build-max-silent-time");
    get(buildTimeout, "build-timeout");
    get(reservedSize, "gc-reserved-space");
    get(fsyncMetadata, "fsync-metadata");
    get(useSQLiteWAL, "use-sqlite-wal");
    get(syncBeforeRegistering, "sync-before-registering");
    get(useSubstitutes, "build-use-substitutes");
    get(buildUsersGroup, "build-users-group");
    get(useChroot, "build-use-chroot");
    get(dirsInChroot, "build-chroot-dirs");
    get(impersonateLinux26, "build-impersonate-linux-26");
    get(keepLog, "build-keep-log");
    get(compressLog, "build-compress-log");
    get(cacheFailure, "build-cache-failure");
    get(pollInterval, "build-poll-interval");
    get(checkRootReachability, "gc-check-reachability");
    get(gcKeepOutputs, "gc-keep-outputs");
    get(gcKeepDerivations, "gc-keep-derivations");
    get(autoOptimiseStore, "auto-optimise-store");
    get(envKeepDerivations, "env-keep-derivations");
    get(daemonUseCgroups, "daemon-use-cgroups");
}


void Settings::get(string & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    res = i->second;
}


void Settings::get(bool & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    if (i->second == "true") res = true;
    else if (i->second == "false") res = false;
    else throw Error(format("configuration option `%1%' should be either `true' or `false', not `%2%'")
        % name % i->second);
}


void Settings::get(StringSet & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    res.clear();
    Strings ss = tokenizeString<Strings>(i->second);
    res.insert(ss.begin(), ss.end());
}


template<class N> void Settings::get(N & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    if (!string2Int(i->second, res))
        throw Error(format("configuration setting `%1%' should have an integer value") % name);
}


string Settings::pack()
{
    string s;
    foreach (SettingsMap::iterator, i, settings) {
        if (i->first.find('\n') != string::npos ||
            i->first.find('=') != string::npos ||
            i->second.find('\n') != string::npos)
            throw Error("illegal option name/value");
        s += i->first; s += '='; s += i->second; s += '\n';
    }
    return s;
}


Settings::SettingsMap Settings::getOverrides()
{
    return overrides;
}


const string nixVersion = NIX_VERSION;


}

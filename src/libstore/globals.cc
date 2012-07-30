#include "config.h"

#include "globals.hh"
#include "util.hh"

#include <map>
#include <algorithm>


namespace nix {


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
    autoOptimiseStore = true;
    envKeepDerivations = false;
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

    string subs = getEnv("NIX_SUBSTITUTERS", "default");
    if (subs == "default") {
        substituters.push_back(nixLibexecDir + "/nix/substituters/copy-from-other-stores.pl");
        substituters.push_back(nixLibexecDir + "/nix/substituters/download-using-manifests.pl");
        substituters.push_back(nixLibexecDir + "/nix/substituters/download-from-binary-cache.pl");
    } else
        substituters = tokenizeString(subs, ":");
}


string & at(Strings & ss, unsigned int n)
{
    Strings::iterator i = ss.begin();
    advance(i, n);
    return *i;
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

        Strings tokens = tokenizeString(line);
        if (tokens.empty()) continue;

        if (tokens.size() < 2 || at(tokens, 1) != "=")
            throw Error(format("illegal configuration line `%1%' in `%2%'") % line % settingsFile);

        string name = at(tokens, 0);

        Strings::iterator i = tokens.begin();
        advance(i, 2);
        settings[name] = concatStringsSep(" ", Strings(i, tokens.end())); // FIXME: slow
    };
}


void Settings::set(const string & name, const string & value)
{
    settings[name] = value;
}


void Settings::update()
{
    get(thisSystem, "system");
    get(maxBuildJobs, "build-max-jobs");
    get(buildCores, "build-cores");
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


void Settings::get(PathSet & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    res.clear();
    Strings ss = tokenizeString(i->second);
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
        s += i->first; s += '='; s += i->second; s += '\n';
    }
    return s;
}


}

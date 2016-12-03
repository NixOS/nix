#include "config.h"

#include "globals.hh"
#include "util.hh"
#include "archive.hh"

#include <map>
#include <algorithm>
#include <unistd.h>


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
    maxBuildJobs = 1;
    buildCores = 1;
#ifdef _SC_NPROCESSORS_ONLN
    long res = sysconf(_SC_NPROCESSORS_ONLN);
    if (res > 0) buildCores = res;
#endif
    readOnlyMode = false;
    thisSystem = SYSTEM;
    maxSilentTime = 0;
    buildTimeout = 0;
    useBuildHook = true;
    reservedSize = 8 * 1024 * 1024;
    fsyncMetadata = true;
    useSQLiteWAL = true;
    syncBeforeRegistering = false;
    useSubstitutes = true;
    buildUsersGroup = getuid() == 0 ? "nixbld" : "";
    useSshSubstituter = true;
    impersonateLinux26 = false;
    keepLog = true;
    compressLog = true;
    maxLogSize = 0;
    pollInterval = 5;
    checkRootReachability = false;
    gcKeepOutputs = false;
    gcKeepDerivations = true;
    autoOptimiseStore = false;
    envKeepDerivations = false;
    lockCPU = getEnv("NIX_AFFINITY_HACK", "1") == "1";
    showTrace = false;
    enableImportNative = false;

    ipfsAPIHost = "localhost";
    ipfsAPIPort = 5001;
    useIpfsGateway = false;
    ipfsGatewayURL = "https://ipfs.io";
}


void Settings::processEnvironment()
{
    nixPrefix = NIX_PREFIX;
    nixStore = canonPath(getEnv("NIX_STORE_DIR", getEnv("NIX_STORE", NIX_STORE_DIR)));
    nixDataDir = canonPath(getEnv("NIX_DATA_DIR", NIX_DATA_DIR));
    nixLogDir = canonPath(getEnv("NIX_LOG_DIR", NIX_LOG_DIR));
    nixStateDir = canonPath(getEnv("NIX_STATE_DIR", NIX_STATE_DIR));
    nixConfDir = canonPath(getEnv("NIX_CONF_DIR", NIX_CONF_DIR));
    nixLibexecDir = canonPath(getEnv("NIX_LIBEXEC_DIR", NIX_LIBEXEC_DIR));
    nixBinDir = canonPath(getEnv("NIX_BIN_DIR", NIX_BIN_DIR));
    nixDaemonSocketFile = canonPath(nixStateDir + DEFAULT_SOCKET_PATH);

    // should be set with the other config options, but depends on nixLibexecDir
#ifdef __APPLE__
    preBuildHook = nixLibexecDir + "/nix/resolve-system-dependencies";
#endif
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
            throw Error(format("illegal configuration line ‘%1%’ in ‘%2%’") % line % settingsFile);

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


string Settings::get(const string & name, const string & def)
{
    auto i = settings.find(name);
    if (i == settings.end()) return def;
    return i->second;
}


Strings Settings::get(const string & name, const Strings & def)
{
    auto i = settings.find(name);
    if (i == settings.end()) return def;
    return tokenizeString<Strings>(i->second);
}


bool Settings::get(const string & name, bool def)
{
    bool res = def;
    _get(res, name);
    return res;
}


int Settings::get(const string & name, int def)
{
    int res = def;
    _get(res, name);
    return res;
}


void Settings::update()
{
    _get(tryFallback, "build-fallback");
    _get(maxBuildJobs, "build-max-jobs");
    _get(buildCores, "build-cores");
    _get(thisSystem, "system");
    _get(maxSilentTime, "build-max-silent-time");
    _get(buildTimeout, "build-timeout");
    _get(reservedSize, "gc-reserved-space");
    _get(fsyncMetadata, "fsync-metadata");
    _get(useSQLiteWAL, "use-sqlite-wal");
    _get(syncBeforeRegistering, "sync-before-registering");
    _get(useSubstitutes, "build-use-substitutes");
    _get(buildUsersGroup, "build-users-group");
    _get(impersonateLinux26, "build-impersonate-linux-26");
    _get(keepLog, "build-keep-log");
    _get(compressLog, "build-compress-log");
    _get(maxLogSize, "build-max-log-size");
    _get(pollInterval, "build-poll-interval");
    _get(checkRootReachability, "gc-check-reachability");
    _get(gcKeepOutputs, "gc-keep-outputs");
    _get(gcKeepDerivations, "gc-keep-derivations");
    _get(autoOptimiseStore, "auto-optimise-store");
    _get(envKeepDerivations, "env-keep-derivations");
    _get(sshSubstituterHosts, "ssh-substituter-hosts");
    _get(useSshSubstituter, "use-ssh-substituter");
    _get(logServers, "log-servers");
    _get(enableImportNative, "allow-unsafe-native-code-during-evaluation");
    _get(useCaseHack, "use-case-hack");
    _get(preBuildHook, "pre-build-hook");
    _get(keepGoing, "keep-going");
    _get(keepFailed, "keep-failed");
    _get(ipfsAPIHost, "ipfs-api-host");
    _get(ipfsAPIPort, "ipfs-api-port");
    _get(useIpfsGateway, "use-ipfs-gateway");
    _get(ipfsGatewayURL, "ipfs-gateway-url");
}


void Settings::_get(string & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    res = i->second;
}


void Settings::_get(bool & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    if (i->second == "true") res = true;
    else if (i->second == "false") res = false;
    else throw Error(format("configuration option ‘%1%’ should be either ‘true’ or ‘false’, not ‘%2%’")
        % name % i->second);
}


void Settings::_get(StringSet & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    res.clear();
    Strings ss = tokenizeString<Strings>(i->second);
    res.insert(ss.begin(), ss.end());
}

void Settings::_get(Strings & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    res = tokenizeString<Strings>(i->second);
}


template<class N> void Settings::_get(N & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    if (!string2Int(i->second, res))
        throw Error(format("configuration setting ‘%1%’ should have an integer value") % name);
}


string Settings::pack()
{
    string s;
    for (auto & i : settings) {
        if (i.first.find('\n') != string::npos ||
            i.first.find('=') != string::npos ||
            i.second.find('\n') != string::npos)
            throw Error("illegal option name/value");
        s += i.first; s += '='; s += i.second; s += '\n';
    }
    return s;
}


void Settings::unpack(const string & pack) {
    Strings lines = tokenizeString<Strings>(pack, "\n");
    for (auto & i : lines) {
        string::size_type eq = i.find('=');
        if (eq == string::npos)
            throw Error("illegal option name/value");
        set(i.substr(0, eq), i.substr(eq + 1));
    }
}


Settings::SettingsMap Settings::getOverrides()
{
    return overrides;
}


const string nixVersion = PACKAGE_VERSION;


}

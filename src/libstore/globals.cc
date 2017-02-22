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

/* chroot-like behavior from Apple's sandbox */
#if __APPLE__
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES "/System/Library /usr/lib /dev /bin/sh"
#else
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES ""
#endif

Settings settings;


Settings::Settings()
{
    deprecatedOptions = StringSet({
        "build-use-chroot", "build-chroot-dirs", "build-extra-chroot-dirs",
        "this-option-never-existed-but-who-will-know"
    });

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
    netrcFile = fmt("%s/%s", nixConfDir, "netrc");
    useSandbox = "false"; // TODO: make into an enum

#if __linux__
    sandboxPaths = tokenizeString<StringSet>("/bin/sh=" BASH_PATH);
#endif

    restrictEval = false;
    buildRepeat = 0;
    allowedImpureHostPrefixes = tokenizeString<StringSet>(DEFAULT_ALLOWED_IMPURE_PREFIXES);
    sandboxShmSize = "50%";
    darwinLogSandboxViolations = false;
    runDiffHook = false;
    diffHook = "";
    enforceDeterminism = true;
    binaryCachePublicKeys = Strings();
    secretKeyFiles = Strings();
    binaryCachesParallelConnections = 25;
    enableHttp2 = true;
    tarballTtl = 60 * 60;
    signedBinaryCaches = "";
    substituters = Strings();
    binaryCaches = Strings();
    extraBinaryCaches = Strings();
    trustedUsers = Strings({"root"});
    allowedUsers = Strings({"*"});
    printMissing = true;
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
    _get(netrcFile, "netrc-file");
    _get(useSandbox, "build-use-sandbox", "build-use-chroot");
    _get(sandboxPaths, "build-sandbox-paths", "build-chroot-dirs");
    _get(extraSandboxPaths, "build-extra-sandbox-paths", "build-extra-chroot-dirs");
    _get(restrictEval, "restrict-eval");
    _get(buildRepeat, "build-repeat");
    _get(allowedImpureHostPrefixes, "allowed-impure-host-deps");
    _get(sandboxShmSize, "sandbox-dev-shm-size");
    _get(darwinLogSandboxViolations, "darwin-log-sandbox-violations");
    _get(runDiffHook, "run-diff-hook");
    _get(diffHook, "diff-hook");
    _get(enforceDeterminism, "enforce-determinism");
    _get(binaryCachePublicKeys, "binary-cache-public-keys");
    _get(secretKeyFiles, "secret-key-files");
    _get(binaryCachesParallelConnections, "binary-caches-parallel-connections");
    _get(enableHttp2, "enable-http2");
    _get(tarballTtl, "tarball-ttl");
    _get(signedBinaryCaches, "signed-binary-caches");
    _get(substituters, "substituters");
    _get(binaryCaches, "binary-caches");
    _get(extraBinaryCaches, "extra-binary-caches");
    _get(trustedUsers, "trusted-users");
    _get(allowedUsers, "allowed-users");
    _get(printMissing, "print-missing");

    /* Clear out any deprecated options that might be left, so users know we recognize the option
       but aren't processing it anymore */
    for (auto &i : deprecatedOptions) {
        if (settings.find(i) != settings.end()) {
            printError(format("warning: deprecated option '%1%' is no longer supported and will be ignored") % i);
            settings.erase(i);
        }
    }

    if (settings.size() != 0) {
        string bad;
        for (auto &i : settings)
            bad += "'" + i.first + "', ";
        bad.pop_back();
        bad.pop_back();
        throw Error(format("unrecognized options: %s") % bad);
    }
}

void Settings::checkDeprecated(const string & name)
{
    if (deprecatedOptions.find(name) != deprecatedOptions.end())
        printError(format("warning: deprecated option '%1%' will soon be unsupported") % name);
}

void Settings::_get(string & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
    res = i->second;
}

void Settings::_get(string & res, const string & name1, const string & name2)
{
    SettingsMap::iterator i = settings.find(name1);
    if (i == settings.end()) i = settings.find(name2);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
    res = i->second;
}


void Settings::_get(bool & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
    if (i->second == "true") res = true;
    else if (i->second == "false") res = false;
    else throw Error(format("configuration option ‘%1%’ should be either ‘true’ or ‘false’, not ‘%2%’")
        % name % i->second);
}


void Settings::_get(StringSet & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
    res.clear();
    Strings ss = tokenizeString<Strings>(i->second);
    res.insert(ss.begin(), ss.end());
}

void Settings::_get(StringSet & res, const string & name1, const string & name2)
{
    SettingsMap::iterator i = settings.find(name1);
    if (i == settings.end()) i = settings.find(name2);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
    res.clear();
    Strings ss = tokenizeString<Strings>(i->second);
    res.insert(ss.begin(), ss.end());
}

void Settings::_get(Strings & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
    res = tokenizeString<Strings>(i->second);
}


template<class N> void Settings::_get(N & res, const string & name)
{
    SettingsMap::iterator i = settings.find(name);
    if (i == settings.end()) return;
    checkDeprecated(i->first);
    settings.erase(i);
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

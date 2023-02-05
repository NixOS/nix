#include "globals.hh"
#include "util.hh"
#include "archive.hh"
#include "args.hh"
#include "abstract-setting-to-json.hh"
#include "compute-levels.hh"

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <dlfcn.h>
#include <sys/utsname.h>

#include <nlohmann/json.hpp>

#include <sodium/core.h>

#ifdef __GLIBC__
#include <gnu/lib-names.h>
#include <nss.h>
#include <dlfcn.h>
#endif

namespace nix {


/* The default location of the daemon socket, relative to nixStateDir.
   The socket is in a directory to allow you to control access to the
   Nix daemon by setting the mode/ownership of the directory
   appropriately.  (This wouldn't work on the socket itself since it
   must be deleted and recreated on startup.) */
#define DEFAULT_SOCKET_PATH "/daemon-socket/socket"

Settings settings;

static GlobalConfig::Register rSettings(&settings);

Settings::Settings()
    : nixPrefix(NIX_PREFIX)
    , nixStore(canonPath(getEnvNonEmpty("NIX_STORE_DIR").value_or(getEnvNonEmpty("NIX_STORE").value_or(NIX_STORE_DIR))))
    , nixDataDir(canonPath(getEnvNonEmpty("NIX_DATA_DIR").value_or(NIX_DATA_DIR)))
    , nixLogDir(canonPath(getEnvNonEmpty("NIX_LOG_DIR").value_or(NIX_LOG_DIR)))
    , nixStateDir(canonPath(getEnvNonEmpty("NIX_STATE_DIR").value_or(NIX_STATE_DIR)))
    , nixConfDir(canonPath(getEnvNonEmpty("NIX_CONF_DIR").value_or(NIX_CONF_DIR)))
    , nixUserConfFiles(getUserConfigFiles())
    , nixBinDir(canonPath(getEnvNonEmpty("NIX_BIN_DIR").value_or(NIX_BIN_DIR)))
    , nixManDir(canonPath(NIX_MAN_DIR))
    , nixDaemonSocketFile(canonPath(getEnvNonEmpty("NIX_DAEMON_SOCKET_PATH").value_or(nixStateDir + DEFAULT_SOCKET_PATH)))
{
    buildUsersGroup = getuid() == 0 ? "nixbld" : "";
    allowSymlinkedStore = getEnv("NIX_IGNORE_SYMLINK_STORE") == "1";

    auto sslOverride = getEnv("NIX_SSL_CERT_FILE").value_or(getEnv("SSL_CERT_FILE").value_or(""));
    if (sslOverride != "")
        caFile = sslOverride;

    /* Backwards compatibility. */
    auto s = getEnv("NIX_REMOTE_SYSTEMS");
    if (s) {
        Strings ss;
        for (auto & p : tokenizeString<Strings>(*s, ":"))
            ss.push_back("@" + p);
        builders = concatStringsSep(" ", ss);
    }

#if defined(__linux__) && defined(SANDBOX_SHELL)
    sandboxPaths = tokenizeString<StringSet>("/bin/sh=" SANDBOX_SHELL);
#endif

    /* chroot-like behavior from Apple's sandbox */
#if __APPLE__
    sandboxPaths = tokenizeString<StringSet>("/System/Library/Frameworks /System/Library/PrivateFrameworks /bin/sh /bin/bash /private/tmp /private/var/tmp /usr/lib");
    allowedImpureHostPrefixes = tokenizeString<StringSet>("/System/Library /usr/lib /dev /bin/sh");
#endif

    buildHook = getSelfExe().value_or("nix") + " __build-remote";
}

void loadConfFile()
{
    globalConfig.applyConfigFile(settings.nixConfDir + "/nix.conf");

    /* We only want to send overrides to the daemon, i.e. stuff from
       ~/.nix/nix.conf or the command line. */
    globalConfig.resetOverridden();

    auto files = settings.nixUserConfFiles;
    for (auto file = files.rbegin(); file != files.rend(); file++) {
        globalConfig.applyConfigFile(*file);
    }

    auto nixConfEnv = getEnv("NIX_CONFIG");
    if (nixConfEnv.has_value()) {
        globalConfig.applyConfig(nixConfEnv.value(), "NIX_CONFIG");
    }

}

std::vector<Path> getUserConfigFiles()
{
    // Use the paths specified in NIX_USER_CONF_FILES if it has been defined
    auto nixConfFiles = getEnv("NIX_USER_CONF_FILES");
    if (nixConfFiles.has_value()) {
        return tokenizeString<std::vector<std::string>>(nixConfFiles.value(), ":");
    }

    // Use the paths specified by the XDG spec
    std::vector<Path> files;
    auto dirs = getConfigDirs();
    for (auto & dir : dirs) {
        files.insert(files.end(), dir + "/nix/nix.conf");
    }
    return files;
}

unsigned int Settings::getDefaultCores()
{
    const unsigned int concurrency = std::max(1U, std::thread::hardware_concurrency());
    const unsigned int maxCPU = getMaxCPU();

    if (maxCPU > 0)
      return maxCPU;
    else
      return concurrency;
}

StringSet Settings::getDefaultSystemFeatures()
{
    /* For backwards compatibility, accept some "features" that are
       used in Nixpkgs to route builds to certain machines but don't
       actually require anything special on the machines. */
    StringSet features{"nixos-test", "benchmark", "big-parallel"};

    #if __linux__
    features.insert("uid-range");
    #endif

    #if __linux__
    if (access("/dev/kvm", R_OK | W_OK) == 0)
        features.insert("kvm");
    #endif

    return features;
}

StringSet Settings::getDefaultExtraPlatforms()
{
    StringSet extraPlatforms;

    if (std::string{SYSTEM} == "x86_64-linux" && !isWSL1())
        extraPlatforms.insert("i686-linux");

#if __linux__
    StringSet levels = computeLevels();
    for (auto iter = levels.begin(); iter != levels.end(); ++iter)
        extraPlatforms.insert(*iter + "-linux");
#elif __APPLE__
    // Rosetta 2 emulation layer can run x86_64 binaries on aarch64
    // machines. Note that we can’t force processes from executing
    // x86_64 in aarch64 environments or vice versa since they can
    // always exec with their own binary preferences.
    if (std::string{SYSTEM} == "aarch64-darwin" &&
        runProgram(RunOptions {.program = "arch", .args = {"-arch", "x86_64", "/usr/bin/true"}, .mergeStderrToStdout = true}).first == 0)
        extraPlatforms.insert("x86_64-darwin");
#endif

    return extraPlatforms;
}

bool Settings::isWSL1()
{
    struct utsname utsbuf;
    uname(&utsbuf);
    // WSL1 uses -Microsoft suffix
    // WSL2 uses -microsoft-standard suffix
    return hasSuffix(utsbuf.release, "-Microsoft");
}

Path Settings::getDefaultSSLCertFile()
{
    for (auto & fn : {"/etc/ssl/certs/ca-certificates.crt", "/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"})
        if (pathExists(fn)) return fn;
    return "";
}

const std::string nixVersion = PACKAGE_VERSION;

NLOHMANN_JSON_SERIALIZE_ENUM(SandboxMode, {
    {SandboxMode::smEnabled, true},
    {SandboxMode::smRelaxed, "relaxed"},
    {SandboxMode::smDisabled, false},
});

template<> void BaseSetting<SandboxMode>::set(const std::string & str, bool append)
{
    if (str == "true") value = smEnabled;
    else if (str == "relaxed") value = smRelaxed;
    else if (str == "false") value = smDisabled;
    else throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<> bool BaseSetting<SandboxMode>::isAppendable()
{
    return false;
}

template<> std::string BaseSetting<SandboxMode>::to_string() const
{
    if (value == smEnabled) return "true";
    else if (value == smRelaxed) return "relaxed";
    else if (value == smDisabled) return "false";
    else abort();
}

template<> void BaseSetting<SandboxMode>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = "Enable sandboxing.",
        .category = category,
        .handler = {[this]() { override(smEnabled); }}
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = "Disable sandboxing.",
        .category = category,
        .handler = {[this]() { override(smDisabled); }}
    });
    args.addFlag({
        .longName = "relaxed-" + name,
        .description = "Enable sandboxing, but allow builds to disable it.",
        .category = category,
        .handler = {[this]() { override(smRelaxed); }}
    });
}

void MaxBuildJobsSetting::set(const std::string & str, bool append)
{
    if (str == "auto") value = std::max(1U, std::thread::hardware_concurrency());
    else {
        if (auto n = string2Int<decltype(value)>(str))
            value = *n;
        else
            throw UsageError("configuration setting '%s' should be 'auto' or an integer", name);
    }
}


void PluginFilesSetting::set(const std::string & str, bool append)
{
    if (pluginsLoaded)
        throw UsageError("plugin-files set after plugins were loaded, you may need to move the flag before the subcommand");
    BaseSetting<Paths>::set(str, append);
}


void initPlugins()
{
    assert(!settings.pluginFiles.pluginsLoaded);
    for (const auto & pluginFile : settings.pluginFiles.get()) {
        Paths pluginFiles;
        try {
            auto ents = readDirectory(pluginFile);
            for (const auto & ent : ents)
                pluginFiles.emplace_back(pluginFile + "/" + ent.name);
        } catch (SysError & e) {
            if (e.errNo != ENOTDIR)
                throw;
            pluginFiles.emplace_back(pluginFile);
        }
        for (const auto & file : pluginFiles) {
            /* handle is purposefully leaked as there may be state in the
               DSO needed by the action of the plugin. */
            void *handle =
                dlopen(file.c_str(), RTLD_LAZY | RTLD_LOCAL);
            if (!handle)
                throw Error("could not dynamically open plugin file '%s': %s", file, dlerror());
        }
    }

    /* Since plugins can add settings, try to re-apply previously
       unknown settings. */
    globalConfig.reapplyUnknownSettings();
    globalConfig.warnUnknownSettings();

    /* Tell the user if they try to set plugin-files after we've already loaded */
    settings.pluginFiles.pluginsLoaded = true;
}

static void preloadNSS()
{
    /* builtin:fetchurl can trigger a DNS lookup, which with glibc can trigger a dynamic library load of
       one of the glibc NSS libraries in a sandboxed child, which will fail unless the library's already
       been loaded in the parent. So we force a lookup of an invalid domain to force the NSS machinery to
       load its lookup libraries in the parent before any child gets a chance to. */
    static std::once_flag dns_resolve_flag;

    std::call_once(dns_resolve_flag, []() {
#ifdef __GLIBC__
        /* On linux, glibc will run every lookup through the nss layer.
         * That means every lookup goes, by default, through nscd, which acts as a local
         * cache.
         * Because we run builds in a sandbox, we also remove access to nscd otherwise
         * lookups would leak into the sandbox.
         *
         * But now we have a new problem, we need to make sure the nss_dns backend that
         * does the dns lookups when nscd is not available is loaded or available.
         *
         * We can't make it available without leaking nix's environment, so instead we'll
         * load the backend, and configure nss so it does not try to run dns lookups
         * through nscd.
         *
         * This is technically only used for builtins:fetch* functions so we only care
         * about dns.
         *
         * All other platforms are unaffected.
         */
        if (!dlopen(LIBNSS_DNS_SO, RTLD_NOW))
            warn("unable to load nss_dns backend");
        // FIXME: get hosts entry from nsswitch.conf.
        __nss_configure_lookup("hosts", "files dns");
#endif
    });
}

static bool initLibStoreDone = false;

void assertLibStoreInitialized() {
    if (!initLibStoreDone) {
        printError("The program must call nix::initNix() before calling any libstore library functions.");
        abort();
    };
}

void initLibStore() {

    initLibUtil();

    if (sodium_init() == -1)
        throw Error("could not initialise libsodium");

    loadConfFile();

    preloadNSS();

    /* On macOS, don't use the per-session TMPDIR (as set e.g. by
       sshd). This breaks build users because they don't have access
       to the TMPDIR, in particular in ‘nix-store --serve’. */
#if __APPLE__
    if (hasPrefix(getEnv("TMPDIR").value_or("/tmp"), "/var/folders/"))
        unsetenv("TMPDIR");
#endif

    initLibStoreDone = true;
}


}

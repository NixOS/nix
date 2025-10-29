#include "nix/store/globals.hh"
#include "nix/util/config-global.hh"
#include "nix/util/current-process.hh"
#include "nix/util/archive.hh"
#include "nix/util/args.hh"
#include "nix/util/abstract-setting-to-json.hh"
#include "nix/util/compute-levels.hh"
#include "nix/util/signals.hh"

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#  include <sys/utsname.h>
#endif

#ifdef __GLIBC__
#  include <gnu/lib-names.h>
#  include <nss.h>
#  include <dlfcn.h>
#endif

#ifdef __APPLE__
#  include "nix/util/processes.hh"
#endif

#include "nix/util/config-impl.hh"

#ifdef __APPLE__
#  include <sys/sysctl.h>
#endif

#include "store-config-private.hh"

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
    , nixStore(
#ifndef _WIN32
          // On Windows `/nix/store` is not a canonical path, but we dont'
          // want to deal with that yet.
          canonPath
#endif
          (getEnvNonEmpty("NIX_STORE_DIR").value_or(getEnvNonEmpty("NIX_STORE").value_or(NIX_STORE_DIR))))
    , nixDataDir(canonPath(getEnvNonEmpty("NIX_DATA_DIR").value_or(NIX_DATA_DIR)))
    , nixLogDir(canonPath(getEnvNonEmpty("NIX_LOG_DIR").value_or(NIX_LOG_DIR)))
    , nixStateDir(canonPath(getEnvNonEmpty("NIX_STATE_DIR").value_or(NIX_STATE_DIR)))
    , nixConfDir(canonPath(getEnvNonEmpty("NIX_CONF_DIR").value_or(NIX_CONF_DIR)))
    , nixUserConfFiles(getUserConfigFiles())
    , nixDaemonSocketFile(
          canonPath(getEnvNonEmpty("NIX_DAEMON_SOCKET_PATH").value_or(nixStateDir + DEFAULT_SOCKET_PATH)))
{
#ifndef _WIN32
    buildUsersGroup = isRootUser() ? "nixbld" : "";
#endif
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
        builders = concatStringsSep("\n", ss);
    }

#if (defined(__linux__) || defined(__FreeBSD__)) && defined(SANDBOX_SHELL)
    sandboxPaths = {{"/bin/sh", {.source = SANDBOX_SHELL}}};
#endif

    /* chroot-like behavior from Apple's sandbox */
#ifdef __APPLE__
    for (PathView p : {
             "/System/Library/Frameworks",
             "/System/Library/PrivateFrameworks",
             "/bin/sh",
             "/bin/bash",
             "/private/tmp",
             "/private/var/tmp",
             "/usr/lib",
         }) {
        sandboxPaths.get().insert_or_assign(std::string{p}, ChrootPath{.source = std::string{p}});
    }
    allowedImpureHostPrefixes = tokenizeString<StringSet>("/System/Library /usr/lib /dev /bin/sh");
#endif
}

void loadConfFile(AbstractConfig & config)
{
    auto applyConfigFile = [&](const Path & path) {
        try {
            std::string contents = readFile(path);
            config.applyConfig(contents, path);
        } catch (SystemError &) {
        }
    };

    applyConfigFile(settings.nixConfDir + "/nix.conf");

    /* We only want to send overrides to the daemon, i.e. stuff from
       ~/.nix/nix.conf or the command line. */
    config.resetOverridden();

    auto files = settings.nixUserConfFiles;
    for (auto file = files.rbegin(); file != files.rend(); file++) {
        applyConfigFile(*file);
    }

    auto nixConfEnv = getEnv("NIX_CONFIG");
    if (nixConfEnv.has_value()) {
        config.applyConfig(nixConfEnv.value(), "NIX_CONFIG");
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
        files.insert(files.end(), dir + "/nix.conf");
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

#ifdef __APPLE__
static bool hasVirt()
{

    int hasVMM;
    int hvSupport;
    size_t size;

    size = sizeof(hasVMM);
    if (sysctlbyname("kern.hv_vmm_present", &hasVMM, &size, NULL, 0) == 0) {
        if (hasVMM)
            return false;
    }

    // whether the kernel and hardware supports virt
    size = sizeof(hvSupport);
    if (sysctlbyname("kern.hv_support", &hvSupport, &size, NULL, 0) == 0) {
        return hvSupport == 1;
    } else {
        return false;
    }
}
#endif

StringSet Settings::getDefaultSystemFeatures()
{
    /* For backwards compatibility, accept some "features" that are
       used in Nixpkgs to route builds to certain machines but don't
       actually require anything special on the machines. */
    StringSet features{"nixos-test", "benchmark", "big-parallel"};

#ifdef __linux__
    features.insert("uid-range");
#endif

#ifdef __linux__
    if (access("/dev/kvm", R_OK | W_OK) == 0)
        features.insert("kvm");
#endif

#ifdef __APPLE__
    if (hasVirt())
        features.insert("apple-virt");
#endif

    return features;
}

StringSet Settings::getDefaultExtraPlatforms()
{
    StringSet extraPlatforms;

    if (std::string{NIX_LOCAL_SYSTEM} == "x86_64-linux" && !isWSL1())
        extraPlatforms.insert("i686-linux");

#ifdef __linux__
    StringSet levels = computeLevels();
    for (auto iter = levels.begin(); iter != levels.end(); ++iter)
        extraPlatforms.insert(*iter + "-linux");
#elif defined(__APPLE__)
    // Rosetta 2 emulation layer can run x86_64 binaries on aarch64
    // machines. Note that we can’t force processes from executing
    // x86_64 in aarch64 environments or vice versa since they can
    // always exec with their own binary preferences.
    if (std::string{NIX_LOCAL_SYSTEM} == "aarch64-darwin"
        && runProgram(
               RunOptions{.program = "arch", .args = {"-arch", "x86_64", "/usr/bin/true"}, .mergeStderrToStdout = true})
                   .first
               == 0)
        extraPlatforms.insert("x86_64-darwin");
#endif

    return extraPlatforms;
}

bool Settings::isWSL1()
{
#ifdef __linux__
    struct utsname utsbuf;
    uname(&utsbuf);
    // WSL1 uses -Microsoft suffix
    // WSL2 uses -microsoft-standard suffix
    return hasSuffix(utsbuf.release, "-Microsoft");
#else
    return false;
#endif
}

Path Settings::getDefaultSSLCertFile()
{
    for (auto & fn :
         {"/etc/ssl/certs/ca-certificates.crt", "/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"})
        if (pathAccessible(fn))
            return fn;
    return "";
}

const ExternalBuilder * Settings::findExternalDerivationBuilderIfSupported(const Derivation & drv)
{
    if (auto it = std::ranges::find_if(
            externalBuilders.get(), [&](const auto & handler) { return handler.systems.contains(drv.platform); });
        it != externalBuilders.get().end())
        return &*it;
    return nullptr;
}

std::string nixVersion = PACKAGE_VERSION;

NLOHMANN_JSON_SERIALIZE_ENUM(
    SandboxMode,
    {
        {SandboxMode::smEnabled, true},
        {SandboxMode::smRelaxed, "relaxed"},
        {SandboxMode::smDisabled, false},
    });

template<>
SandboxMode BaseSetting<SandboxMode>::parse(const std::string & str) const
{
    if (str == "true")
        return smEnabled;
    else if (str == "relaxed")
        return smRelaxed;
    else if (str == "false")
        return smDisabled;
    else
        throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<>
struct BaseSetting<SandboxMode>::trait
{
    static constexpr bool appendable = false;
};

template<>
std::string BaseSetting<SandboxMode>::to_string() const
{
    if (value == smEnabled)
        return "true";
    else if (value == smRelaxed)
        return "relaxed";
    else if (value == smDisabled)
        return "false";
    else
        unreachable();
}

template<>
void BaseSetting<SandboxMode>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .aliases = aliases,
        .description = "Enable sandboxing.",
        .category = category,
        .handler = {[this]() { override(smEnabled); }},
    });
    args.addFlag({
        .longName = "no-" + name,
        .aliases = aliases,
        .description = "Disable sandboxing.",
        .category = category,
        .handler = {[this]() { override(smDisabled); }},
    });
    args.addFlag({
        .longName = "relaxed-" + name,
        .aliases = aliases,
        .description = "Enable sandboxing, but allow builds to disable it.",
        .category = category,
        .handler = {[this]() { override(smRelaxed); }},
    });
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ChrootPath, source, optional)

template<>
PathsInChroot BaseSetting<PathsInChroot>::parse(const std::string & str) const
{
    PathsInChroot pathsInChroot;
    for (auto i : tokenizeString<StringSet>(str)) {
        if (i.empty())
            continue;
        bool optional = false;
        if (i[i.size() - 1] == '?') {
            optional = true;
            i.pop_back();
        }
        size_t p = i.find('=');
        std::string inside, outside;
        if (p == std::string::npos) {
            inside = i;
            outside = i;
        } else {
            inside = i.substr(0, p);
            outside = i.substr(p + 1);
        }
        pathsInChroot[inside] = {.source = outside, .optional = optional};
    }
    return pathsInChroot;
}

template<>
std::string BaseSetting<PathsInChroot>::to_string() const
{
    std::vector<std::string> accum;
    for (auto & [name, cp] : value) {
        std::string s = name == cp.source ? name : name + "=" + cp.source;
        if (cp.optional)
            s += "?";
        accum.push_back(std::move(s));
    }
    return concatStringsSep(" ", accum);
}

unsigned int MaxBuildJobsSetting::parse(const std::string & str) const
{
    if (str == "auto")
        return std::max(1U, std::thread::hardware_concurrency());
    else {
        if (auto n = string2Int<decltype(value)>(str))
            return *n;
        else
            throw UsageError("configuration setting '%s' should be 'auto' or an integer", name);
    }
}

template<>
Settings::ExternalBuilders BaseSetting<Settings::ExternalBuilders>::parse(const std::string & str) const
{
    try {
        return nlohmann::json::parse(str).template get<Settings::ExternalBuilders>();
    } catch (std::exception & e) {
        throw UsageError("parsing setting '%s': %s", name, e.what());
    }
}

template<>
std::string BaseSetting<Settings::ExternalBuilders>::to_string() const
{
    return nlohmann::json(value).dump();
}

template<>
void BaseSetting<PathsInChroot>::appendOrSet(PathsInChroot newValue, bool append)
{
    if (!append)
        value.clear();
    value.insert(std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
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

void assertLibStoreInitialized()
{
    if (!initLibStoreDone) {
        printError("The program must call nix::initNix() before calling any libstore library functions.");
        abort();
    };
}

void initLibStore(bool loadConfig)
{
    if (initLibStoreDone)
        return;

    initLibUtil();

    if (loadConfig)
        loadConfFile(globalConfig);

    preloadNSS();

    /* Because of an objc quirk[1], calling curl_global_init for the first time
       after fork() will always result in a crash.
       Up until now the solution has been to set OBJC_DISABLE_INITIALIZE_FORK_SAFETY
       for every nix process to ignore that error.
       Instead of working around that error we address it at the core -
       by calling curl_global_init here, which should mean curl will already
       have been initialized by the time we try to do so in a forked process.

       [1]
       https://github.com/apple-oss-distributions/objc4/blob/01edf1705fbc3ff78a423cd21e03dfc21eb4d780/runtime/objc-initialize.mm#L614-L636
    */
    curl_global_init(CURL_GLOBAL_ALL);
#ifdef __APPLE__
    /* On macOS, don't use the per-session TMPDIR (as set e.g. by
       sshd). This breaks build users because they don't have access
       to the TMPDIR, in particular in ‘nix-store --serve’. */
    if (hasPrefix(defaultTempDir(), "/var/folders/"))
        unsetenv("TMPDIR");
#endif

    initLibStoreDone = true;
}

} // namespace nix

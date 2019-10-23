#include "globals.hh"
#include "util.hh"
#include "archive.hh"
#include "args.hh"

#include <algorithm>
#include <map>
#include <thread>
#ifndef _WIN32
#include <dlfcn.h>
#endif


namespace nix {

#ifndef _WIN32
/* The default location of the daemon socket, relative to nixStateDir.
   The socket is in a directory to allow you to control access to the
   Nix daemon by setting the mode/ownership of the directory
   appropriately.  (This wouldn't work on the socket itself since it
   must be deleted and recreated on startup.) */
#define DEFAULT_SOCKET_PATH "/daemon-socket/socket"
#endif

/* chroot-like behavior from Apple's sandbox */
#if __APPLE__
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES "/System/Library /usr/lib /dev /bin/sh"
#else
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES ""
#endif

Settings settings;

static GlobalConfig::Register r1(&settings);

Settings::Settings()
    : nixStore(canonPath(getEnv("NIX_STORE_DIR", getEnv("NIX_STORE", NIX_STORE_DIR))))
    , nixDataDir(canonPath(getEnv("NIX_DATA_DIR", NIX_DATA_DIR)))
    , nixLogDir(canonPath(getEnv("NIX_LOG_DIR", NIX_LOG_DIR)))
    , nixStateDir(canonPath(getEnv("NIX_STATE_DIR", NIX_STATE_DIR)))
    , nixConfDir(canonPath(getEnv("NIX_CONF_DIR", NIX_CONF_DIR)))
    , nixBinDir(canonPath(getEnv("NIX_BIN_DIR", NIX_BIN_DIR)))
#ifndef _WIN32
    , nixPrefix(NIX_PREFIX)
    , nixLibexecDir(canonPath(getEnv("NIX_LIBEXEC_DIR", NIX_LIBEXEC_DIR)))
    , nixManDir(canonPath(NIX_MAN_DIR))
    , nixDaemonSocketFile(canonPath(nixStateDir + DEFAULT_SOCKET_PATH))
#endif
{
    fprintf(stderr, "NixStore=%s\n",        nixStore.c_str());
    fprintf(stderr, "NixDataDir=%s\n",      nixDataDir.c_str());
    fprintf(stderr, "NixLogDir=%s\n",       nixLogDir.c_str());
    fprintf(stderr, "NixStateDir=%s\n",     nixStateDir.c_str());
    fprintf(stderr, "NixConfDir=%s\n",      nixConfDir.c_str());
    fprintf(stderr, "NixBinDir=%s\n",       nixBinDir.c_str());
    assert(!nixStore.empty());
    assert(!nixDataDir.empty());
    assert(!nixLogDir.empty());
    assert(!nixStateDir.empty());
    assert(!nixConfDir.empty());
    assert(!nixBinDir.empty());

#ifndef _WIN32
    buildUsersGroup = getuid() == 0 ? "nixbld" : "";
#endif
    lockCPU = getEnv("NIX_AFFINITY_HACK", "1") == "1";

    caFile = getEnv("NIX_SSL_CERT_FILE", getEnv("SSL_CERT_FILE", ""));
    if (caFile == "") {
        for (auto & fn : {
#ifndef _WIN32
                          "/etc/ssl/certs/ca-certificates.crt",
#else
                          "C:/msys64/usr/ssl/certs/ca-bundle.crt", // BUGBUG!!!
#endif
                          "/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"})
            if (pathExists(fn)) {
                caFile = fn;
                break;
            }
    }

    /* Backwards compatibility. */
    auto s = getEnv("NIX_REMOTE_SYSTEMS");
    if (s != "") {
        Strings ss;
        for (auto & p : tokenizeString<Strings>(s, ":"))
            ss.push_back("@" + p);
        builders = concatStringsSep(" ", ss);
    }

#if defined(__linux__) && defined(SANDBOX_SHELL)
    sandboxPaths = tokenizeString<StringSet>("/bin/sh=" SANDBOX_SHELL);
#endif

    allowedImpureHostPrefixes = tokenizeString<StringSet>(DEFAULT_ALLOWED_IMPURE_PREFIXES);
}

void loadConfFile()
{
    globalConfig.applyConfigFile(settings.nixConfDir + "/nix.conf");

    /* We only want to send overrides to the daemon, i.e. stuff from
       ~/.nix/nix.conf or the command line. */
    globalConfig.resetOverriden();

    globalConfig.applyConfigFile(getConfigDir() + "/nix/nix.conf");
}

unsigned int Settings::getDefaultCores()
{
    return std::max(1U, std::thread::hardware_concurrency());
}

const string nixVersion = PACKAGE_VERSION;

template<> void BaseSetting<SandboxMode>::set(const std::string & str)
{
    if (str == "true") value = smEnabled;
    else if (str == "relaxed") value = smRelaxed;
    else if (str == "false") value = smDisabled;
    else throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<> std::string BaseSetting<SandboxMode>::to_string()
{
    if (value == smEnabled) return "true";
    else if (value == smRelaxed) return "relaxed";
    else if (value == smDisabled) return "false";
    else abort();
}

template<> void BaseSetting<SandboxMode>::toJSON(JSONPlaceholder & out)
{
    AbstractSetting::toJSON(out);
}

template<> void BaseSetting<SandboxMode>::convertToArg(Args & args, const std::string & category)
{
    args.mkFlag()
        .longName(name)
        .description("Enable sandboxing.")
        .handler([=](std::vector<std::string> ss) { override(smEnabled); })
        .category(category);
    args.mkFlag()
        .longName("no-" + name)
        .description("Disable sandboxing.")
        .handler([=](std::vector<std::string> ss) { override(smDisabled); })
        .category(category);
    args.mkFlag()
        .longName("relaxed-" + name)
        .description("Enable sandboxing, but allow builds to disable it.")
        .handler([=](std::vector<std::string> ss) { override(smRelaxed); })
        .category(category);
}

void MaxBuildJobsSetting::set(const std::string & str)
{
    if (str == "auto") value = std::max(1U, std::thread::hardware_concurrency());
    else if (!string2Int(str, value))
        throw UsageError("configuration setting '%s' should be 'auto' or an integer", name);
}


void initPlugins()
{
#ifndef _WIN32
    for (const auto & pluginFile : settings.pluginFiles.get()) {
        Paths pluginFiles;
        try {
            auto ents = readDirectory(pluginFile);
            for (const auto & ent : ents)
                pluginFiles.emplace_back(pluginFile + "/" + ent.name);
        } catch (PosixError & e) {
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
#endif
}

}

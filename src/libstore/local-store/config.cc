#include <nlohmann/json.hpp>

#include "args.hh"
#include "local-store/config.hh"
#include "abstract-setting-to-json.hh"

namespace nix {

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

template class BaseSetting<SandboxMode>;

LocalStoreConfig::LocalStoreConfig(const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , sandboxPaths{
        (StoreConfig*) this,
#if defined(__linux__) && defined(SANDBOX_SHELL)
        tokenizeString<StringSet>("/bin/sh=" SANDBOX_SHELL),
#else
        {},
#endif
        "sandbox-paths",
        R"(
          A list of paths bind-mounted into Nix sandbox environments. You can
          use the syntax `target=source` to mount a path in a different
          location in the sandbox; for instance, `/bin=/nix-bin` will mount
          the path `/nix-bin` as `/bin` inside the sandbox. If *source* is
          followed by `?`, then it is not an error if *source* does not exist;
          for example, `/dev/nvidiactl?` specifies that `/dev/nvidiactl` will
          only be mounted in the sandbox if it exists in the host filesystem.

          If the source is in the Nix store, then its closure will be added to
          the sandbox as well.

          Depending on how Nix was built, the default value for this option
          may be empty or provide `/bin/sh` as a bind-mount of `bash`.
        )",
        {"build-chroot-dirs", "build-sandbox-paths"}}
    , allowSymlinkedStore{
        (StoreConfig*) this,
        getEnv("NIX_IGNORE_SYMLINK_STORE") == "1",
        "allow-symlinked-store",
        R"(
          If set to `true`, Nix will stop complaining if the store directory
          (typically /nix/store) contains symlink components.

          This risks making some builds "impure" because builders sometimes
          "canonicalise" paths by resolving all symlink components. Problems
          occur if those builds are then deployed to machines where /nix/store
          resolves to a different location from that of the build machine. You
          can enable this setting if you are sure you're not going to do that.
        )"}
{ }

//LocalStoreConfig hack { StringMap {} };

//GlobalConfig::Register rHack(&hack);

}

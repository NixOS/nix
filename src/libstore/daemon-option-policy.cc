#include "nix/store/daemon-option-policy.hh"
#include "nix/store/globals.hh"
#include "nix/store/filetransfer.hh"

namespace nix {

static const StringSet clientOptions{
    "store",
    "print-missing",
    "use-xdg-base-directories",
};

static const StringSet fixedOptions{
    "keep-failed",
    "keep-going",
    "fallback",
    "max-jobs",
    "max-silent-time",
    "cores",
    "substitute",
};

/*
 * Settings eligible for the free-form SetOptions override map.
 * New settings must be explicitly assigned a transport classification.
 */
static const StringSet overrideOptions{
    "keep-build-log",
    "compress-build-log",
    "narinfo-cache-negative-ttl",
    "narinfo-cache-positive-ttl",
    "narinfo-cache-meta-ttl",
    "use-sqlite-wal",
    "system",
    "trusted-public-keys",
    "secret-key-files",
    "require-sigs",
    "extra-platforms",
    "system-features",
    "trusted-substituters",
    "warn-large-path-threshold",
    "log-lines",
    "max-substitution-jobs",
    "timeout",
    "build-hook",
    "build-hook-kill-timeout",
    "builders",
    "always-allow-substitutes",
    "builders-use-substitutes",
    "substituters",
    "max-build-log-size",
    "build-poll-interval",
    "post-build-hook",
    "gc-reserved-space",
    "keep-outputs",
    "keep-derivations",
    "min-free",
    "max-free",
    "min-free-check-interval",
    "start-id",
    "id-count",
    "fsync-metadata",
    "fsync-store-paths",
    "sync-before-registering",
    "auto-optimise-store",
    "nar-buffer-size",
    "allow-symlinked-store",
    "build-users-group",
    "auto-allocate-uids",
    "use-cgroups",
    "impersonate-linux-26",
    "sandbox",
    "sandbox-paths",
    "sandbox-fallback",
    "require-drop-supplementary-groups",
    "sandbox-dev-shm-size",
    "sandbox-build-dir",
    "build-dir",
    "allowed-impure-host-deps",
    "darwin-log-sandbox-violations",
    "run-diff-hook",
    "diff-hook",
    "pre-build-hook",
    "filter-syscalls",
    "allow-new-privileges",
    "ignored-acls",
    "impure-env",
    "hashed-mirrors",
    "external-builders",
    "http2",
    "http3",
    "user-agent-suffix",
    "http-connections",
    "connect-timeout",
    "stalled-download-timeout",
    "filetransfer-retry-attempts",
    "filetransfer-retry-delay",
    "filetransfer-retry-delay-rate-limited",
    "filetransfer-retry-max-delay",
    "filetransfer-retry-jitter",
    "download-buffer-size",
    "download-speed",
    "netrc-file",
    "ssl-cert-file",
};

std::optional<DaemonOptionKind> classifyDaemonOption(const std::string & name)
{
    if (clientOptions.contains(name))
        return DaemonOptionKind::ClientOnly;
    if (fixedOptions.contains(name))
        return DaemonOptionKind::Fixed;
    if (overrideOptions.contains(name))
        return DaemonOptionKind::Override;
    return std::nullopt;
}

bool daemonOptionAllowsValue(const std::string & rule, const std::string & value)
{
    return rule == daemonOptionRuleAny || rule == daemonOptionRuleSubstituters
           || (rule == daemonOptionRuleEmpty && value.empty());
}

bool daemonOptionIsAccepted(const StringMap & policy, const std::string & name, const std::string & value)
{
    auto i = policy.find(name);
    return i != policy.end() && daemonOptionAllowsValue(i->second, value);
}

StringMap
getDaemonOptionPolicy(const Settings & settings, const FileTransferSettings & fileTransferSettings, bool trusted)
{
    static const StringMap untrusted{
        {"timeout", std::string{daemonOptionRuleAny}},
        {"build-poll-interval", std::string{daemonOptionRuleAny}},
        {"connect-timeout", std::string{daemonOptionRuleAny}},
        {"builders", std::string{daemonOptionRuleEmpty}},
        {"substituters", std::string{daemonOptionRuleSubstituters}},
    };

    std::map<std::string, Config::SettingInfo> known;
    settings.getSettings(known);
    fileTransferSettings.getSettings(known);
    StringMap result;
    for (auto & [name, _] : known) {
        if (classifyDaemonOption(name) != DaemonOptionKind::Override)
            continue;
        if (trusted)
            result.emplace(name, daemonOptionRuleAny);
        else if (auto i = untrusted.find(name); i != untrusted.end())
            result.insert(*i);
    }
    return result;
}

ForwardedDaemonOptions getForwardedDaemonOptions(
    const Settings & settings,
    const FileTransferSettings & fileTransferSettings,
    const std::optional<StringMap> & daemonPolicy)
{
    auto forwardingPolicy = getDaemonOptionPolicy(settings, fileTransferSettings, true);
    std::map<std::string, Config::SettingInfo> overridden;
    settings.getSettings(overridden, true);
    fileTransferSettings.getSettings(overridden, true);
    ForwardedDaemonOptions result;
    for (auto & [name, info] : overridden) {
        if (classifyDaemonOption(name) != DaemonOptionKind::Override || !forwardingPolicy.contains(name))
            continue;
        if (daemonPolicy) {
            if (!daemonOptionIsAccepted(*daemonPolicy, name, info.value)) {
                result.rejected.insert(name);
                continue;
            }
        }
        result.overrides.emplace(name, info.value);
    }
    return result;
}

} // namespace nix

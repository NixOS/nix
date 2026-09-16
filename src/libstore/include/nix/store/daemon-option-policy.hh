#pragma once

#include "nix/util/types.hh"
#include <optional>

namespace nix {

class Settings;
class FileTransferSettings;

enum class DaemonOptionKind {
    /** Used only by the client process. */
    ClientOnly,
    /** Encoded as a fixed, positional SetOptions field. */
    Fixed,
    /** Eligible for the free-form SetOptions override map. */
    Override,
};

/**
 * Rules used in the value of the daemon option policy map.
 *
 * Unknown rules must be treated as rejecting the option. This allows newer
 * daemons to introduce stricter rules without older clients accidentally
 * weakening them.
 */
inline constexpr std::string_view daemonOptionRuleAny = "any";
inline constexpr std::string_view daemonOptionRuleEmpty = "empty";
inline constexpr std::string_view daemonOptionRuleSubstituters = "substituters";

/**
 * Return the policy for the free-form overrides in WorkerProto::Op::SetOptions.
 *
 * An absent option is not accepted. The fixed fields at the start of
 * SetOptions are deliberately absent because they are governed by the legacy
 * protocol, rather than this policy.
 */
StringMap
getDaemonOptionPolicy(const Settings & settings, const FileTransferSettings & fileTransferSettings, bool trusted);

/** Check an option value against a rule received from the daemon. */
bool daemonOptionAllowsValue(const std::string & rule, const std::string & value);

/** Check whether a policy accepts a named option and value. */
bool daemonOptionIsAccepted(const StringMap & policy, const std::string & name, const std::string & value);

/** Return the transport classification of a known libstore setting. */
std::optional<DaemonOptionKind> classifyDaemonOption(const std::string & name);

struct ForwardedDaemonOptions
{
    StringMap overrides;
    StringSet rejected;
};

/**
 * Collect locally overridden settings that are meaningful to a daemon.
 *
 * If daemonPolicy is present, it is authoritative: options missing from it or
 * failing its value rule are returned in rejected. If it is absent, the peer
 * predates the policy feature and all locally classified daemon options are
 * forwarded for compatibility; the daemon still enforces trust restrictions.
 */
ForwardedDaemonOptions getForwardedDaemonOptions(
    const Settings & settings,
    const FileTransferSettings & fileTransferSettings,
    const std::optional<StringMap> & daemonPolicy);

} // namespace nix

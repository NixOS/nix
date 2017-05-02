#include "machines.hh"
#include "util.hh"
#include "globals.hh"

#include <algorithm>

namespace nix {

Machine::Machine(decltype(storeUri) storeUri,
    decltype(systemTypes) systemTypes,
    decltype(sshKey) sshKey,
    decltype(maxJobs) maxJobs,
    decltype(speedFactor) speedFactor,
    decltype(supportedFeatures) supportedFeatures,
    decltype(mandatoryFeatures) mandatoryFeatures) :
    storeUri(
        // Backwards compatibility: if the URI is a hostname,
        // prepend ssh://.
        storeUri.find("://") != std::string::npos || hasPrefix(storeUri, "local") || hasPrefix(storeUri, "remote") || hasPrefix(storeUri, "auto")
        ? storeUri
        : "ssh://" + storeUri),
    systemTypes(systemTypes),
    sshKey(sshKey),
    maxJobs(maxJobs),
    speedFactor(std::max(1U, speedFactor)),
    supportedFeatures(supportedFeatures),
    mandatoryFeatures(mandatoryFeatures)
{}

bool Machine::allSupported(const std::set<string> & features) const {
    return std::all_of(features.begin(), features.end(),
        [&](const string & feature) {
            return supportedFeatures.count(feature) ||
                mandatoryFeatures.count(feature);
        });
}

bool Machine::mandatoryMet(const std::set<string> & features) const {
    return std::all_of(mandatoryFeatures.begin(), mandatoryFeatures.end(),
        [&](const string & feature) {
            return features.count(feature);
        });
}

void parseMachines(const std::string & s, Machines & machines)
{
    for (auto line : tokenizeString<std::vector<string>>(s, "\n")) {
        chomp(line);
        line.erase(std::find(line.begin(), line.end(), '#'), line.end());
        if (line.empty()) continue;
        auto tokens = tokenizeString<std::vector<string>>(line);
        auto sz = tokens.size();
        if (sz < 1)
            throw FormatError("bad machine specification ‘%s’", line);
        machines.emplace_back(tokens[0],
            sz >= 2 ? tokenizeString<std::vector<string>>(tokens[1], ",") : std::vector<string>{settings.thisSystem},
            sz >= 3 ? tokens[2] : "",
            sz >= 4 ? std::stoull(tokens[3]) : 1LL,
            sz >= 5 ? std::stoull(tokens[4]) : 1LL,
            sz >= 6 ? tokenizeString<std::set<string>>(tokens[5], ",") : std::set<string>{},
            sz >= 7 ? tokenizeString<std::set<string>>(tokens[6], ",") : std::set<string>{});
    }
}

}

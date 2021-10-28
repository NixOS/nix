#include "machines.hh"
#include "util.hh"
#include "globals.hh"
#include "store-api.hh"

#include <algorithm>

namespace nix {

Machine::Machine(decltype(storeUri) storeUri,
    decltype(systemTypes) systemTypes,
    decltype(sshKey) sshKey,
    decltype(maxJobs) maxJobs,
    decltype(speedFactor) speedFactor,
    decltype(supportedFeatures) supportedFeatures,
    decltype(mandatoryFeatures) mandatoryFeatures,
    decltype(sshPublicHostKey) sshPublicHostKey) :
    storeUri(
        // Backwards compatibility: if the URI is schemeless, is not a path,
        // and is not one of the special store connection words, prepend
        // ssh://.
        storeUri.find("://") != std::string::npos
        || storeUri.find("/") != std::string::npos
        || storeUri == "auto"
        || storeUri == "daemon"
        || storeUri == "local"
        || hasPrefix(storeUri, "auto?")
        || hasPrefix(storeUri, "daemon?")
        || hasPrefix(storeUri, "local?")
        || hasPrefix(storeUri, "?")
        ? storeUri
        : "ssh://" + storeUri),
    systemTypes(systemTypes),
    sshKey(sshKey),
    maxJobs(maxJobs),
    speedFactor(std::max(1U, speedFactor)),
    supportedFeatures(supportedFeatures),
    mandatoryFeatures(mandatoryFeatures),
    sshPublicHostKey(sshPublicHostKey)
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

ref<Store> Machine::openStore() const {
    Store::Params storeParams;
    if (hasPrefix(storeUri, "ssh://")) {
        storeParams["max-connections"] = "1";
        storeParams["log-fd"] = "4";
    }

    if (hasPrefix(storeUri, "ssh://") || hasPrefix(storeUri, "ssh-ng://")) {
        if (sshKey != "")
            storeParams["ssh-key"] = sshKey;
        if (sshPublicHostKey != "")
            storeParams["base64-ssh-public-host-key"] = sshPublicHostKey;
    }

    {
        auto & fs = storeParams["system-features"];
        auto append = [&](auto feats) {
            for (auto & f : feats) {
                if (fs.size() > 0) fs += ' ';
                fs += f;
            }
        };
        append(supportedFeatures);
        append(mandatoryFeatures);
    }

    return nix::openStore(storeUri, storeParams);
}

unsigned long long stoull_machine_arg(std::vector<std::string> & machine, int at, const std::string & type)
{
    try {
        return std::stoull(machine[at]);
    } catch (std::invalid_argument) {
        throw FormatError(
            "Cannot " ANSI_BOLD "stoull" ANSI_NORMAL " '%s'! Argument '%d' is supposed to be a '%s'!",
            machine[at],
            at + 1,
            type
        );
    }
}

void parseMachines(const std::string & s, Machines & machines)
{
    for (auto line : tokenizeString<std::vector<string>>(s, "\n;")) {
        trim(line);
        line.erase(std::find(line.begin(), line.end(), '#'), line.end());
        if (line.empty()) continue;

        if (line[0] == '@') {
            auto file = trim(std::string(line, 1));
            try {
                parseMachines(readFile(file), machines);
            } catch (const SysError & e) {
                if (e.errNo != ENOENT)
                    throw;
                debug("cannot find machines file '%s'", file);
            }
            continue;
        }

        auto tokens = tokenizeString<std::vector<string>>(line);
        auto sz = tokens.size();
        if (sz < 1)
            throw FormatError("bad machine specification '%s'", line);

        auto isSet = [&](size_t n) {
            return tokens.size() > n && tokens[n] != "" && tokens[n] != "-";
        };

        machines.emplace_back(tokens[0],
            isSet(1) ? tokenizeString<std::vector<string>>(tokens[1], ",") : std::vector<string>{settings.thisSystem},
            isSet(2) ? tokens[2] : "",
            isSet(3) ? stoull_machine_arg(tokens, 3, "max jobs") : 1LL,
            isSet(4) ? stoull_machine_arg(tokens, 4, "speed factor") : 1LL,
            isSet(5) ? tokenizeString<std::set<string>>(tokens[5], ",") : std::set<string>{},
            isSet(6) ? tokenizeString<std::set<string>>(tokens[6], ",") : std::set<string>{},
            isSet(7) ? tokens[7] : "");
    }
}

Machines getMachines()
{
    static auto machines = [&]() {
        Machines machines;
        parseMachines(settings.builders, machines);
        return machines;
    }();
    return machines;
}

}

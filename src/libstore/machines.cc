#include "machines.hh"
#include "build-capability.hh"
#include "globals.hh"
#include "store-api.hh"

#include <algorithm>

namespace nix {

Machine::Machine(decltype(storeUri) storeUri,
     std::set<std::string> systemTypes,
    decltype(sshKey) sshKey,
    unsigned int maxJobs,
    float speedFactor,
    std::set<std::string> supportedFeatures,
    std::set<std::string> mandatoryFeatures,
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
    sshKey(sshKey),
    sshPublicHostKey(sshPublicHostKey)
{
    if (speedFactor < 0.0)
        throw UsageError("speed factor must be >= 0");

    for (const auto & system : systemTypes) {
        SchedulableCapability * cap =
            &capabilities.emplace_back(SchedulableCapability {
                .capability = BuildCapability {
                    .system = system,
                    .supportedFeatures = supportedFeatures,
                    .mandatoryFeatures = mandatoryFeatures
                },
                .maxJobs = maxJobs,
                .isLocal = false,
                .speedFactor = speedFactor
            });
        capabilitiesBySystem[system].push_back(cap);
    }
}

bool Machine::canBuild(const Schedulable & schedulable) const
{
    auto system = schedulable.getSystem();

    if (system == "builtin") {
        // Buildable on any system.
        return std::any_of(capabilities.begin(), capabilities.end(),
            [&](const SchedulableCapability & sc) {
                return sc.capability.canBuild(schedulable);
            });
    }

    auto it = capabilitiesBySystem.find(std::string(system));
    if (it == capabilitiesBySystem.end())
        return false;

    auto & capsList = it->second;

    return std::any_of(capsList.cbegin(), capsList.cend(),
        [&](const SchedulableCapability * sc) {
            return sc->capability.canBuild(schedulable);
        });
}

bool Machine::systemSupported(const std::string & system) const
{
    return system == "builtin" || capabilitiesBySystem.contains(system);
}

bool Machine::allSupported(const std::set<std::string> & features) const
{
    // We need to use any_of because this method doesn't know the `system`.
    // This is not accurate; hence the deprecation.
    return std::any_of(capabilities.begin(), capabilities.end(),
        [&](const SchedulableCapability & sc) {
            return std::all_of(features.begin(), features.end(),
                [&](const std::string & feature) {
                    return sc.capability.supportedFeatures.count(feature) ||
                        sc.capability.mandatoryFeatures.count(feature);
                });
        });
}

bool Machine::mandatoryMet(const std::set<std::string> & features) const
{
    // We need to use any_of because this method doesn't know the `system`.
    // This is not accurate; hence the deprecation.
    return std::any_of(capabilities.begin(), capabilities.end(),
        [&](const SchedulableCapability & sc) {
            return std::all_of(sc.capability.mandatoryFeatures.begin(), sc.capability.mandatoryFeatures.end(),
                [&](const std::string & feature) {
                    return features.count(feature);
                });
        });
}

ref<Store> Machine::openStore() const
{
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
        // FIXME
        // auto & fs = storeParams["system-features"];
        // auto append = [&](auto feats) {
        //     for (auto & f : feats) {
        //         if (fs.size() > 0) fs += ' ';
        //         fs += f;
        //     }
        // };
        // append(supportedFeatures);
        // append(mandatoryFeatures);
    }

    return nix::openStore(storeUri, storeParams);
}

static std::vector<std::string> expandBuilderLines(const std::string & builders)
{
    std::vector<std::string> result;
    for (auto line : tokenizeString<std::vector<std::string>>(builders, "\n;")) {
        trim(line);
        line.erase(std::find(line.begin(), line.end(), '#'), line.end());
        if (line.empty()) continue;

        if (line[0] == '@') {
            const std::string path = trim(std::string(line, 1));
            std::string text;
            try {
                text = readFile(path);
            } catch (const SysError & e) {
                if (e.errNo != ENOENT)
                    throw;
                debug("cannot find machines file '%s'", path);
            }

            const auto lines = expandBuilderLines(text);
            result.insert(end(result), begin(lines), end(lines));
            continue;
        }

        result.emplace_back(line);
    }
    return result;
}

static Machine parseBuilderLine(const std::string & line)
{
    const auto tokens = tokenizeString<std::vector<std::string>>(line);

    auto isSet = [&](size_t fieldIndex) {
        return tokens.size() > fieldIndex && tokens[fieldIndex] != "" && tokens[fieldIndex] != "-";
    };

    auto parseUnsignedIntField = [&](size_t fieldIndex) {
        const auto result = string2Int<unsigned int>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError("bad machine specification: failed to convert column #%lu in a row: '%s' to 'unsigned int'", fieldIndex, line);
        }
        return result.value();
    };

    auto parseFloatField = [&](size_t fieldIndex) {
        const auto result = string2Int<float>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError("bad machine specification: failed to convert column #%lu in a row: '%s' to 'float'", fieldIndex, line);
        }
        return result.value();
    };

    auto ensureBase64 = [&](size_t fieldIndex) {
        const auto & str = tokens[fieldIndex];
        try {
            base64Decode(str);
        } catch (const Error & e) {
            throw FormatError("bad machine specification: a column #%lu in a row: '%s' is not valid base64 string: %s", fieldIndex, line, e.what());
        }
        return str;
    };

    if (!isSet(0))
        throw FormatError("bad machine specification: store URL was not found at the first column of a row: '%s'", line);

    return {
        tokens[0],
        isSet(1) ? tokenizeString<std::set<std::string>>(tokens[1], ",") : std::set<std::string>{settings.thisSystem},
        isSet(2) ? tokens[2] : "",
        isSet(3) ? parseUnsignedIntField(3) : 1U,
        isSet(4) ? parseFloatField(4) : 1.0f,
        isSet(5) ? tokenizeString<std::set<std::string>>(tokens[5], ",") : std::set<std::string>{},
        isSet(6) ? tokenizeString<std::set<std::string>>(tokens[6], ",") : std::set<std::string>{},
        isSet(7) ? ensureBase64(7) : ""
    };
}

static Machines parseBuilderLines(const std::vector<std::string> & builders)
{
    Machines result;
    std::transform(builders.begin(), builders.end(), std::back_inserter(result), parseBuilderLine);
    return result;
}

Machines getMachines()
{
    const auto builderLines = expandBuilderLines(settings.builders);
    return parseBuilderLines(builderLines);
}

}

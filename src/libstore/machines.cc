#include "machines.hh"
#include "globals.hh"
#include "store-api.hh"

#include <algorithm>

namespace nix {

Machine::Machine(
    const std::string & storeUri,
    decltype(systemTypes) systemTypes,
    decltype(sshKey) sshKey,
    decltype(maxJobs) maxJobs,
    decltype(speedFactor) speedFactor,
    decltype(supportedFeatures) supportedFeatures,
    decltype(mandatoryFeatures) mandatoryFeatures,
    decltype(sshPublicHostKey) sshPublicHostKey) :
    storeUri(StoreReference::parse(
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
        : "ssh://" + storeUri)),
    systemTypes(systemTypes),
    sshKey(sshKey),
    maxJobs(maxJobs),
    speedFactor(speedFactor == 0.0f ? 1.0f : speedFactor),
    supportedFeatures(supportedFeatures),
    mandatoryFeatures(mandatoryFeatures),
    sshPublicHostKey(sshPublicHostKey)
{
    if (speedFactor < 0.0)
        throw UsageError("speed factor must be >= 0");
}

bool Machine::systemSupported(const std::string & system) const
{
    return system == "builtin" || (systemTypes.count(system) > 0);
}

bool Machine::allSupported(const std::set<std::string> & features) const
{
    return std::all_of(features.begin(), features.end(),
        [&](const std::string & feature) {
            return supportedFeatures.count(feature) ||
                mandatoryFeatures.count(feature);
        });
}

bool Machine::mandatoryMet(const std::set<std::string> & features) const
{
    return std::all_of(mandatoryFeatures.begin(), mandatoryFeatures.end(),
        [&](const std::string & feature) {
            return features.count(feature);
        });
}

StoreReference Machine::completeStoreReference() const
{
    auto storeUri = this->storeUri;

    auto * generic = std::get_if<StoreReference::Specified>(&storeUri.variant);

    if (generic && generic->scheme == "ssh") {
        storeUri.params["max-connections"] = "1";
        storeUri.params["log-fd"] = "4";
    }

    if (generic && (generic->scheme == "ssh" || generic->scheme == "ssh-ng")) {
        if (sshKey != "")
            storeUri.params["ssh-key"] = sshKey;
        if (sshPublicHostKey != "")
            storeUri.params["base64-ssh-public-host-key"] = sshPublicHostKey;
    }

    {
        auto & fs = storeUri.params["system-features"];
        auto append = [&](auto feats) {
            for (auto & f : feats) {
                if (fs.size() > 0) fs += ' ';
                fs += f;
            }
        };
        append(supportedFeatures);
        append(mandatoryFeatures);
    }

    return storeUri;
}

ref<Store> Machine::openStore() const
{
    return nix::openStore(completeStoreReference());
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

static Machine parseBuilderLine(const std::set<std::string> & defaultSystems, const std::string & line)
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
        const auto result = string2Float<float>(tokens[fieldIndex]);
        if (!result) {
            throw FormatError("bad machine specification: failed to convert column #%lu in a row: '%s' to 'float'", fieldIndex, line);
        }
        return result.value();
    };

    auto ensureBase64 = [&](size_t fieldIndex) {
        const auto & str = tokens[fieldIndex];
        try {
            base64Decode(str);
        } catch (FormatError & e) {
            e.addTrace({}, "while parsing machine specification at a column #%lu in a row: '%s'", fieldIndex, line);
            throw;
        }
        return str;
    };

    if (!isSet(0))
        throw FormatError("bad machine specification: store URL was not found at the first column of a row: '%s'", line);

    // TODO use designated initializers, once C++ supports those with
    // custom constructors.
    return {
        // `storeUri`
        tokens[0],
        // `systemTypes`
        isSet(1) ? tokenizeString<std::set<std::string>>(tokens[1], ",") : defaultSystems,
        // `sshKey`
        isSet(2) ? tokens[2] : "",
        // `maxJobs`
        isSet(3) ? parseUnsignedIntField(3) : 1U,
        // `speedFactor`
        isSet(4) ? parseFloatField(4) : 1.0f,
        // `supportedFeatures`
        isSet(5) ? tokenizeString<std::set<std::string>>(tokens[5], ",") : std::set<std::string>{},
        // `mandatoryFeatures`
        isSet(6) ? tokenizeString<std::set<std::string>>(tokens[6], ",") : std::set<std::string>{},
        // `sshPublicHostKey`
        isSet(7) ? ensureBase64(7) : ""
    };
}

static Machines parseBuilderLines(const std::set<std::string> & defaultSystems, const std::vector<std::string> & builders)
{
    Machines result;
    std::transform(
        builders.begin(), builders.end(), std::back_inserter(result),
        [&](auto && line) { return parseBuilderLine(defaultSystems, line); });
    return result;
}

Machines Machine::parseConfig(const std::set<std::string> & defaultSystems, const std::string & s)
{
    const auto builderLines = expandBuilderLines(s);
    return parseBuilderLines(defaultSystems, builderLines);
}

Machines getMachines()
{
    return Machine::parseConfig({settings.thisSystem}, settings.builders);
}

}

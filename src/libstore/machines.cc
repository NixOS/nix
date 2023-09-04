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
        isSet(1) ? tokenizeString<std::vector<std::string>>(tokens[1], ",") : std::vector<std::string>{settings.thisSystem},
        isSet(2) ? tokens[2] : "",
        isSet(3) ? parseUnsignedIntField(3) : 1U,
        isSet(4) ? parseUnsignedIntField(4) : 1U,
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

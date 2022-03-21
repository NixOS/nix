#include "flake.hh"
#include "globals.hh"
#include "fetch-settings.hh"

#include <nlohmann/json.hpp>

namespace nix::flake {

// setting name -> setting value -> allow or ignore.
typedef std::map<std::string, std::map<std::string, bool>> TrustedList;

Path trustedListPath()
{
    return getDataDir() + "/nix/trusted-settings.json";
}

static TrustedList readTrustedList()
{
    auto path = trustedListPath();
    if (!pathExists(path)) return {};
    auto json = nlohmann::json::parse(readFile(path));
    return json;
}

static void writeTrustedList(const TrustedList & trustedList)
{
    auto path = trustedListPath();
    createDirs(dirOf(path));
    writeFile(path, nlohmann::json(trustedList).dump());
}

void ConfigFile::apply()
{
    std::set<std::string> whitelist{"bash-prompt", "bash-prompt-suffix", "flake-registry"};

    for (auto & [name, value] : settings) {

        auto baseName = hasPrefix(name, "extra-") ? std::string(name, 6) : name;

        // FIXME: Move into libutil/config.cc.
        std::string valueS;
        if (auto* s = std::get_if<std::string>(&value))
            valueS = *s;
        else if (auto* n = std::get_if<int64_t>(&value))
            valueS = fmt("%d", *n);
        else if (auto* b = std::get_if<Explicit<bool>>(&value))
            valueS = b->t ? "true" : "false";
        else if (auto ss = std::get_if<std::vector<std::string>>(&value))
            valueS = concatStringsSep(" ", *ss); // FIXME: evil
        else
            assert(false);

        if (!whitelist.count(baseName)) {
            auto trustedList = readTrustedList();

            bool trusted = false;
            if (nix::fetchSettings.acceptFlakeConfig){
                trusted = true;
            } else if (auto saved = get(get(trustedList, name).value_or(std::map<std::string, bool>()), valueS)) {
                trusted = *saved;
                warn("Using saved setting for '%s = %s' from ~/.local/share/nix/trusted-settings.json.", name,valueS);
            } else {
                // FIXME: filter ANSI escapes, newlines, \r, etc.
                if (std::tolower(logger->ask(fmt("do you want to allow configuration setting '%s' to be set to '" ANSI_RED "%s" ANSI_NORMAL "' (y/N)?", name, valueS)).value_or('n')) == 'y') {
                    trusted = true;
                }
                if (std::tolower(logger->ask(fmt("do you want to permanently mark this value as %s (y/N)?",  trusted ? "trusted": "untrusted" )).value_or('n')) == 'y') {
                    trustedList[name][valueS] = trusted;
                    writeTrustedList(trustedList);
                }
            }

            if (!trusted) {
                warn("ignoring untrusted flake configuration setting '%s'", name);
                continue;
            }
        }

        globalConfig.set(name, valueS);
    }
}

}

#include "common-args.hh"
#include "globals.hh"
#include "loggers.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const string & programName)
    : programName(programName)
{
    addFlag({
        .longName = "verbose",
        .shortName = 'v',
        .description = "increase verbosity level",
        .handler = {[]() { verbosity = (Verbosity) (verbosity + 1); }},
    });

    addFlag({
        .longName = "quiet",
        .description = "decrease verbosity level",
        .handler = {[]() { verbosity = verbosity > lvlError ? (Verbosity) (verbosity - 1) : lvlError; }},
    });

    addFlag({
        .longName = "debug",
        .description = "enable debug output",
        .handler = {[]() { verbosity = lvlDebug; }},
    });

    addFlag({
        .longName = "option",
        .description = "set a Nix configuration option (overriding nix.conf)",
        .labels = {"name", "value"},
        .handler = {[](std::string name, std::string value) {
            try {
                globalConfig.set(name, value);
            } catch (UsageError & e) {
                if (!completions)
                    warn(e.what());
            }
        }},
        .completer = [](size_t index, std::string_view prefix) {
            if (index == 0) {
                std::map<std::string, Config::SettingInfo> settings;
                globalConfig.getSettings(settings);
                for (auto & s : settings)
                    if (hasPrefix(s.first, prefix))
                        completions->insert(s.first);
            }
        }
    });

    addFlag({
        .longName = "log-format",
        .description = "format of log output; \"raw\", \"internal-json\", \"bar\" "
                        "or \"bar-with-logs\"",
        .labels = {"format"},
        .handler = {[](std::string format) { setLogFormat(format); }},
    });

    addFlag({
        .longName = "max-jobs",
        .shortName = 'j',
        .description = "maximum number of parallel builds",
        .labels = Strings{"jobs"},
        .handler = {[=](std::string s) {
            settings.set("max-jobs", s);
        }}
    });

    std::string cat = "config";
    globalConfig.convertToArgs(*this, cat);

    // Backward compatibility hack: nix-env already had a --system flag.
    if (programName == "nix-env") longFlags.erase("system");

    hiddenCategories.insert(cat);
}

}

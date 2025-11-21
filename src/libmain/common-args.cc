#include <nlohmann/json.hpp>

#include "nix/main/common-args.hh"
#include "nix/util/args/root.hh"
#include "nix/util/config-global.hh"
#include "nix/store/globals.hh"
#include "nix/util/logging.hh"
#include "nix/main/loggers.hh"
#include "nix/util/util.hh"
#include "nix/main/plugin.hh"
#include "nix/main/shared.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const std::string & programName)
    : programName(programName)
{
    addFlag({
        .longName = "verbose",
        .shortName = 'v',
        .description = "Increase the logging verbosity level.",
        .category = loggingCategory,
        .handler = {[]() {
            verbosity = (Verbosity) std::min<std::underlying_type_t<Verbosity>>(verbosity + 1, lvlVomit);
        }},
    });

    addFlag({
        .longName = "quiet",
        .description = "Decrease the logging verbosity level.",
        .category = loggingCategory,
        .handler = {[]() { verbosity = verbosity > lvlError ? (Verbosity) (verbosity - 1) : lvlError; }},
    });

    addFlag({
        .longName = "debug",
        .description = "Set the logging verbosity level to 'debug'.",
        .category = loggingCategory,
        .handler = {[]() { verbosity = lvlDebug; }},
    });

    addFlag({
        .longName = "option",
        .description = "Set the Nix configuration setting *name* to *value* (overriding `nix.conf`).",
        .category = miscCategory,
        .labels = {"name", "value"},
        .handler = {[this](std::string name, std::string value) {
            try {
                globalConfig.set(name, value);
            } catch (UsageError & e) {
                if (!getRoot().completions)
                    warn(e.what());
            }
        }},
        .completer =
            [](AddCompletions & completions, size_t index, std::string_view prefix) {
                if (index == 0) {
                    std::map<std::string, Config::SettingInfo> settings;
                    globalConfig.getSettings(settings);
                    for (auto & s : settings)
                        if (hasPrefix(s.first, prefix))
                            completions.add(s.first, fmt("Set the `%s` setting.", s.first));
                }
            },
    });

    addFlag({
        .longName = "log-format",
        .description = "Set the format of log output; one of `raw`, `internal-json`, `bar` or `bar-with-logs`.",
        .category = loggingCategory,
        .labels = {"format"},
        .handler = {[](std::string format) { setLogFormat(format); }},
    });

    addFlag({
        .longName = "max-jobs",
        .shortName = 'j',
        .description = "The maximum number of parallel builds.",
        .labels = Strings{"jobs"},
        .handler = {[=](std::string s) { settings.set("max-jobs", s); }},
    });

    std::string cat = "Options to override configuration settings";
    globalConfig.convertToArgs(*this, cat);

    // Backward compatibility hack: nix-env already had a --system flag.
    if (programName == "nix-env")
        longFlags.erase("system");

    hiddenCategories.insert(cat);
}

void MixCommonArgs::initialFlagsProcessed()
{
    initPlugins();
    pluginsInited();
}

template<typename T, typename>
void MixPrintJSON::printJSON(const T /* nlohmann::json */ & json)
{
    auto suspension = logger->suspend();
    if (outputPretty) {
        logger->writeToStdout(json.dump(2));
    } else {
        logger->writeToStdout(json.dump());
    }
}

template void MixPrintJSON::printJSON(const nlohmann::json & json);

} // namespace nix

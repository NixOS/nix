#include "common-args.hh"
#include "globals.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const string & programName)
    : programName(programName)
{
    addFlag({
        .longName = "verbose",
        .shortName = 'v',
        .description = "increase verbosity level",
        .handler = {[]() { verbosity = (Verbosity) ((uint64_t) verbosity + 1); }},
    });

    addFlag({
        .longName = "quiet",
        .description = "decrease verbosity level",
        .handler = {[]() { verbosity = verbosity > Verbosity::Error
            ? (Verbosity) ((uint64_t) verbosity - 1)
            : Verbosity::Error; }},
    });

    addFlag({
        .longName = "debug",
        .description = "enable debug output",
        .handler = {[]() { verbosity = Verbosity::Debug; }},
    });

    addFlag({
        .longName = "option",
        .description = "set a Nix configuration option (overriding nix.conf)",
        .labels = {"name", "value"},
        .handler = {[](std::string name, std::string value) {
            try {
                globalConfig.set(name, value);
            } catch (UsageError & e) {
                warn(e.what());
            }
        }},
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

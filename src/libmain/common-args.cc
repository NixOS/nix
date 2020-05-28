#include "common-args.hh"
#include "globals.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const string & programName)
    : programName(programName)
{
<<<<<<< HEAD
    mkFlag()
        .longName("verbose")
        .shortName('v')
        .description("increase verbosity level")
        .handler([]() { verbosity = (Verbosity) ((uint64_t) verbosity + 1); });

    mkFlag()
        .longName("quiet")
        .description("decrease verbosity level")
        .handler([]() { verbosity = verbosity > Verbosity::Error
            ? (Verbosity) ((uint64_t) verbosity - 1)
            : Verbosity::Error; });

    mkFlag()
        .longName("debug")
        .description("enable debug output")
        .handler([]() { verbosity = Verbosity::Debug; });

    mkFlag()
        .longName("option")
        .labels({"name", "value"})
        .description("set a Nix configuration option (overriding nix.conf)")
        .arity(2)
        .handler([](std::vector<std::string> ss) {
||||||| merged common ancestors
    mkFlag()
        .longName("verbose")
        .shortName('v')
        .description("increase verbosity level")
        .handler([]() { verbosity = (Verbosity) (verbosity + 1); });

    mkFlag()
        .longName("quiet")
        .description("decrease verbosity level")
        .handler([]() { verbosity = verbosity > lvlError ? (Verbosity) (verbosity - 1) : lvlError; });

    mkFlag()
        .longName("debug")
        .description("enable debug output")
        .handler([]() { verbosity = lvlDebug; });

    mkFlag()
        .longName("option")
        .labels({"name", "value"})
        .description("set a Nix configuration option (overriding nix.conf)")
        .arity(2)
        .handler([](std::vector<std::string> ss) {
=======
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
>>>>>>> f60ce4fa207a210e23a1142d3a8ead611526e6e1
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

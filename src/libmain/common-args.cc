#include "common-args.hh"
#include "globals.hh"
#include "download.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const string & programName)
    : programName(programName)
{
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
            try {
                globalConfig.set(ss[0], ss[1]);
            } catch (UsageError & e) {
                warn(e.what());
            }
        });

    mkFlag()
        .longName("no-net")
        .description("disable substituters and consider all previously downloaded files up-to-date")
        .handler([]() {
            settings.useSubstitutes = false;
            settings.tarballTtl = std::numeric_limits<unsigned int>::max();
            downloadSettings.tries = 0;
            downloadSettings.connectTimeout = 1;
        });

    std::string cat = "config";
    globalConfig.convertToArgs(*this, cat);

    // Backward compatibility hack: nix-env already had a --system flag.
    if (programName == "nix-env") longFlags.erase("system");

    hiddenCategories.insert(cat);
}

}

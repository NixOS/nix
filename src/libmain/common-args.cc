#include "common-args.hh"
#include "globals.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const string & programName)
    : programName(programName)
{
    mkFlag('v', "verbose", "increase verbosity level", []() {
        verbosity = (Verbosity) (verbosity + 1);
    });

    mkFlag(0, "quiet", "decrease verbosity level", []() {
        verbosity = verbosity > lvlError ? (Verbosity) (verbosity - 1) : lvlError;
    });

    mkFlag(0, "debug", "enable debug output", []() {
        verbosity = lvlDebug;
    });

    mkFlag()
        .longName("option")
        .labels({"name", "value"})
        .description("set a Nix configuration option (overriding nix.conf)")
        .arity(2)
        .handler([](Strings ss) {
            auto name = ss.front(); ss.pop_front();
            auto value = ss.front();
            try {
                settings.set(name, value);
            } catch (UsageError & e) {
                warn(e.what());
            }
        });
}

}

#include "common-args.hh"
#include "globals.hh"

namespace nix {

MixCommonArgs::MixCommonArgs(const string & programName)
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

    mkFlag1(0, "log-type", "type", "set logging format ('pretty', 'flat', 'systemd')",
        [](std::string s) {
            if (s == "pretty") logType = ltPretty;
            else if (s == "escapes") logType = ltEscapes;
            else if (s == "flat") logType = ltFlat;
            else if (s == "systemd") logType = ltSystemd;
            else throw UsageError("unknown log type");
        });

    mkFlag(0, "option", {"name", "value"}, "set a Nix configuration option (overriding nix.conf)", 2,
        [](Strings ss) {
            auto name = ss.front(); ss.pop_front();
            auto value = ss.front();
            settings.set(name, value);
        });
}

}

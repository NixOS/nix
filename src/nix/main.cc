#include <algorithm>

#include "command.hh"
#include "common-args.hh"
#include "eval.hh"
#include "globals.hh"
#include "legacy.hh"
#include "shared.hh"
#include "store-api.hh"
#include "progress-bar.hh"

namespace nix {

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs
{
    NixArgs() : MultiCommand(*RegisterCommand::commands), MixCommonArgs("nix")
    {
        mkFlag('h', "help", "show usage information", [=]() {
            printHelp(programName, std::cout);
            std::cout << "\nNote: this program is EXPERIMENTAL and subject to change.\n";
            throw Exit();
        });

        mkFlag(0, "version", "show version information", std::bind(printVersion, programName));
    }
};

void mainWrapped(int argc, char * * argv)
{
    settings.verboseBuild = false;

    initNix();
    initGC();

    string programName = baseNameOf(argv[0]);

    {
        auto legacy = (*RegisterLegacyCommand::commands)[programName];
        if (legacy) return legacy(argc, argv);
    }

    NixArgs args;

    args.parseCmdline(argvToStrings(argc, argv));

    assert(args.command);

    StartProgressBar bar;

    args.command->prepare();
    args.command->run();
}

}

int main(int argc, char * * argv)
{
    return nix::handleExceptions(argv[0], [&]() {
        nix::mainWrapped(argc, argv);
    });
}

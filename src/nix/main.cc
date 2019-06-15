#include <algorithm>

#include "command.hh"
#include "common-args.hh"
#include "eval.hh"
#include "globals.hh"
#include "legacy.hh"
#include "shared.hh"
#include "store-api.hh"
#include "progress-bar.hh"
#include "finally.hh"

extern std::string chrootHelperName;

void chrootHelper(int argc, char * * argv);

namespace nix {

std::string programPath;

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs
{
    bool printBuildLogs = false;

    NixArgs() : MultiCommand(*RegisterCommand::commands), MixCommonArgs("nix")
    {
        mkFlag()
            .longName("help")
            .description("show usage information")
            .handler([&]() { showHelpAndExit(); });

        mkFlag()
            .longName("help-config")
            .description("show configuration options")
            .handler([&]() {
                std::cout << "The following configuration options are available:\n\n";
                Table2 tbl;
                std::map<std::string, Config::SettingInfo> settings;
                globalConfig.getSettings(settings);
                for (const auto & s : settings)
                    tbl.emplace_back(s.first, s.second.description);
                printTable(std::cout, tbl);
                throw Exit();
            });

        mkFlag()
            .longName("print-build-logs")
            .shortName('L')
            .description("print full build logs on stderr")
            .set(&printBuildLogs, true);

        mkFlag()
            .longName("version")
            .description("show version information")
            .handler([&]() { printVersion(programName); });
    }

    void printFlags(std::ostream & out) override
    {
        Args::printFlags(out);
        std::cout <<
            "\n"
            "In addition, most configuration settings can be overriden using '--<name> <value>'.\n"
            "Boolean settings can be overriden using '--<name>' or '--no-<name>'. See 'nix\n"
            "--help-config' for a list of configuration settings.\n";
    }

    void showHelpAndExit()
    {
        printHelp(programName, std::cout);
        std::cout << "\nNote: this program is EXPERIMENTAL and subject to change.\n";
        throw Exit();
    }
};

void mainWrapped(int argc, char * * argv)
{
    /* The chroot helper needs to be run before any threads have been
       started. */
    if (argc > 0 && argv[0] == chrootHelperName) {
        chrootHelper(argc, argv);
        return;
    }

    initNix();
    initGC();

    programPath = argv[0];
    string programName = baseNameOf(programPath);

    {
        auto legacy = (*RegisterLegacyCommand::commands)[programName];
        if (legacy) return legacy(argc, argv);
    }

    verbosity = lvlError;
    settings.verboseBuild = false;

    NixArgs args;

    args.parseCmdline(argvToStrings(argc, argv));

    initPlugins();

    if (!args.command) args.showHelpAndExit();

    Finally f([]() { stopProgressBar(); });

    startProgressBar(args.printBuildLogs);

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

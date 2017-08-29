#include <algorithm>

#include "command.hh"
#include "common-args.hh"
#include "eval.hh"
#include "globals.hh"
#include "legacy.hh"
#include "shared.hh"
#include "store-api.hh"
#include "progress-bar.hh"

extern std::string chrootHelperName;

void chrootHelper(int argc, char * * argv);

namespace nix {

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs
{
    NixArgs() : MultiCommand(*RegisterCommand::commands), MixCommonArgs("nix")
    {
        mkFlag('h', "help", "show usage information", [&]() { showHelpAndExit(); });

        mkFlag(0, "help-config", "show configuration options", [=]() {
            std::cout << "The following configuration options are available:\n\n";
            Table2 tbl;
            for (const auto & s : settings._getSettings())
                if (!s.second.isAlias)
                    tbl.emplace_back(s.first, s.second.setting->description);
            printTable(std::cout, tbl);
            throw Exit();
        });

        mkFlag(0, "version", "show version information", std::bind(printVersion, programName));

        std::string cat = "config";
        settings.convertToArgs(*this, cat);
        hiddenCategories.insert(cat);
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
    verbosity = lvlError;
    settings.verboseBuild = false;

    /* The chroot helper needs to be run before any threads have been
       started. */
    if (argc > 0 && argv[0] == chrootHelperName) {
        chrootHelper(argc, argv);
        return;
    }

    initNix();
    initGC();

    string programName = baseNameOf(argv[0]);

    {
        auto legacy = (*RegisterLegacyCommand::commands)[programName];
        if (legacy) return legacy(argc, argv);
    }

    NixArgs args;

    args.parseCmdline(argvToStrings(argc, argv));

    if (!args.command) args.showHelpAndExit();

    if (isatty(STDERR_FILENO))
        startProgressBar();

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

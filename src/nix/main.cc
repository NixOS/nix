#include <algorithm>

#include "command.hh"
#include "common-args.hh"
#include "eval.hh"
#include "globals.hh"
#include "legacy.hh"
#include "shared.hh"
#include "store-api.hh"
#include "filetransfer.hh"
#include "finally.hh"
#include "loggers.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>

extern std::string chrootHelperName;

void chrootHelper(int argc, char * * argv);

namespace nix {

/* Check if we have a non-loopback/link-local network interface. */
static bool haveInternet()
{
    struct ifaddrs * addrs;

    if (getifaddrs(&addrs))
        return true;

    Finally free([&]() { freeifaddrs(addrs); });

    for (auto i = addrs; i; i = i->ifa_next) {
        if (!i->ifa_addr) continue;
        if (i->ifa_addr->sa_family == AF_INET) {
            if (ntohl(((sockaddr_in *) i->ifa_addr)->sin_addr.s_addr) != INADDR_LOOPBACK) {
                return true;
            }
        } else if (i->ifa_addr->sa_family == AF_INET6) {
            if (!IN6_IS_ADDR_LOOPBACK(&((sockaddr_in6 *) i->ifa_addr)->sin6_addr) &&
                !IN6_IS_ADDR_LINKLOCAL(&((sockaddr_in6 *) i->ifa_addr)->sin6_addr))
                return true;
        }
    }

    return false;
}

std::string programPath;

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs
{
    bool printBuildLogs = false;
    bool useNet = true;
    bool refresh = false;

    NixArgs() : MultiCommand(*RegisterCommand::commands), MixCommonArgs("nix")
    {
        categories.clear();
        categories[Command::catDefault] = "Main commands";
        categories[catSecondary] = "Infrequently used commands";
        categories[catUtility] = "Utility/scripting commands";
        categories[catNixInstallation] = "Commands for upgrading or troubleshooting your Nix installation";

        addFlag({
            .longName = "help",
            .description = "show usage information",
            .handler = {[&]() { if (!completions) showHelpAndExit(); }},
        });

        addFlag({
            .longName = "help-config",
            .description = "show configuration options",
            .handler = {[&]() {
                std::cout << "The following configuration options are available:\n\n";
                Table2 tbl;
                std::map<std::string, Config::SettingInfo> settings;
                globalConfig.getSettings(settings);
                for (const auto & s : settings)
                    tbl.emplace_back(s.first, s.second.description);
                printTable(std::cout, tbl);
                throw Exit();
            }},
        });

        addFlag({
            .longName = "print-build-logs",
            .shortName = 'L',
            .description = "print full build logs on stderr",
            .handler = {[&]() {setLogFormat(LogFormat::barWithLogs); }},
        });

        addFlag({
            .longName = "version",
            .description = "show version information",
            .handler = {[&]() { if (!completions) printVersion(programName); }},
        });

        addFlag({
            .longName = "no-net",
            .description = "disable substituters and consider all previously downloaded files up-to-date",
            .handler = {[&]() { useNet = false; }},
        });

        addFlag({
            .longName = "refresh",
            .description = "consider all previously downloaded files out-of-date",
            .handler = {[&]() { refresh = true; }},
        });

        deprecatedAliases.insert({"dev-shell", "develop"});
    }

    void printFlags(std::ostream & out) override
    {
        Args::printFlags(out);
        std::cout <<
            "\n"
            "In addition, most configuration settings can be overriden using '--" ANSI_ITALIC "name value" ANSI_NORMAL "'.\n"
            "Boolean settings can be overriden using '--" ANSI_ITALIC "name" ANSI_NORMAL "' or '--no-" ANSI_ITALIC "name" ANSI_NORMAL "'. See 'nix\n"
            "--help-config' for a list of configuration settings.\n";
    }

    void printHelp(const string & programName, std::ostream & out) override
    {
        MultiCommand::printHelp(programName, out);

#if 0
        out << "\nFor full documentation, run 'man " << programName << "' or 'man " << programName << "-" ANSI_ITALIC "COMMAND" ANSI_NORMAL "'.\n";
#endif

        std::cout << "\nNote: this program is " ANSI_RED "EXPERIMENTAL" ANSI_NORMAL " and subject to change.\n";
    }

    void showHelpAndExit()
    {
        printHelp(programName, std::cout);
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
    auto programName = std::string(baseNameOf(programPath));

    {
        auto legacy = (*RegisterLegacyCommand::commands)[programName];
        if (legacy) return legacy(argc, argv);
    }

    verbosity = lvlWarn;
    settings.verboseBuild = false;
    evalSettings.pureEval = true;

    setLogFormat("bar");

    Finally f([] { logger->stop(); });

    NixArgs args;

    Finally printCompletions([&]()
    {
        if (completions) {
            std::cout << (pathCompletions ? "filenames\n" : "no-filenames\n");
            for (auto & s : *completions)
                std::cout << s << "\n";
        }
    });

    try {
        args.parseCmdline(argvToStrings(argc, argv));
    } catch (UsageError &) {
        if (!completions) throw;
    }

    if (completions) return;

    initPlugins();

    if (!args.command) args.showHelpAndExit();

    if (args.command->first != "repl"
        && args.command->first != "doctor"
        && args.command->first != "upgrade-nix")
        settings.requireExperimentalFeature("nix-command");

    if (args.useNet && !haveInternet()) {
        warn("you don't have Internet access; disabling some network-dependent features");
        args.useNet = false;
    }

    if (!args.useNet) {
        // FIXME: should check for command line overrides only.
        if (!settings.useSubstitutes.overriden)
            settings.useSubstitutes = false;
        if (!settings.tarballTtl.overriden)
            settings.tarballTtl = std::numeric_limits<unsigned int>::max();
        if (!fileTransferSettings.tries.overriden)
            fileTransferSettings.tries = 0;
        if (!fileTransferSettings.connectTimeout.overriden)
            fileTransferSettings.connectTimeout = 1;
    }

    if (args.refresh)
        settings.tarballTtl = 0;

    args.command->second->prepare();
    args.command->second->run();
}

}

int main(int argc, char * * argv)
{
    return nix::handleExceptions(argv[0], [&]() {
        nix::mainWrapped(argc, argv);
    });
}

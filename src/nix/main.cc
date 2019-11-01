#include <algorithm>

#include "command.hh"
#include "common-args.hh"
#include "eval.hh"
#include "globals.hh"
#include "legacy.hh"
#include "shared.hh"
#include "store-api.hh"
#include "progress-bar.hh"
#include "download.hh"
#include "finally.hh"

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

        mkFlag()
            .longName("no-net")
            .description("disable substituters and consider all previously downloaded files up-to-date")
            .handler([&]() { useNet = false; });
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

    verbosity = lvlWarn;
    settings.verboseBuild = false;

    NixArgs args;

    args.parseCmdline(argvToStrings(argc, argv));

    initPlugins();

    if (!args.command) args.showHelpAndExit();

    Finally f([]() { stopProgressBar(); });

    startProgressBar(args.printBuildLogs);

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
        if (!downloadSettings.tries.overriden)
            downloadSettings.tries = 0;
        if (!downloadSettings.connectTimeout.overriden)
            downloadSettings.connectTimeout = 1;
    }

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

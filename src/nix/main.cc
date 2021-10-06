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
#include "markdown.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>

#include <nlohmann/json.hpp>

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
char * * savedArgv;

struct HelpRequested { };

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs
{
    bool printBuildLogs = false;
    bool useNet = true;
    bool refresh = false;
    bool showVersion = false;

    NixArgs() : MultiCommand(RegisterCommand::getCommandsFor({})), MixCommonArgs("nix")
    {
        categories.clear();
        categories[Command::catDefault] = "Main commands";
        categories[catSecondary] = "Infrequently used commands";
        categories[catUtility] = "Utility/scripting commands";
        categories[catNixInstallation] = "Commands for upgrading or troubleshooting your Nix installation";

        addFlag({
            .longName = "help",
            .description = "Show usage information.",
            .handler = {[&]() { throw HelpRequested(); }},
        });

        addFlag({
            .longName = "print-build-logs",
            .shortName = 'L',
            .description = "Print full build logs on standard error.",
            .category = loggingCategory,
            .handler = {[&]() {setLogFormat(LogFormat::barWithLogs); }},
        });

        addFlag({
            .longName = "version",
            .description = "Show version information.",
            .handler = {[&]() { showVersion = true; }},
        });

        addFlag({
            .longName = "offline",
            .aliases = {"no-net"}, // FIXME: remove
            .description = "Disable substituters and consider all previously downloaded files up-to-date.",
            .handler = {[&]() { useNet = false; }},
        });

        addFlag({
            .longName = "refresh",
            .description = "Consider all previously downloaded files out-of-date.",
            .handler = {[&]() { refresh = true; }},
        });
    }

    std::map<std::string, std::vector<std::string>> aliases = {
        {"add-to-store", {"store", "add-path"}},
        {"cat-nar", {"nar", "cat"}},
        {"cat-store", {"store", "cat"}},
        {"copy-sigs", {"store", "copy-sigs"}},
        {"dev-shell", {"develop"}},
        {"diff-closures", {"store", "diff-closures"}},
        {"dump-path", {"store", "dump-path"}},
        {"hash-file", {"hash", "file"}},
        {"hash-path", {"hash", "path"}},
        {"ls-nar", {"nar", "ls"}},
        {"ls-store", {"store", "ls"}},
        {"make-content-addressable", {"store", "make-content-addressable"}},
        {"optimise-store", {"store", "optimise"}},
        {"ping-store", {"store", "ping"}},
        {"sign-paths", {"store", "sign"}},
        {"to-base16", {"hash", "to-base16"}},
        {"to-base32", {"hash", "to-base32"}},
        {"to-base64", {"hash", "to-base64"}},
        {"verify", {"store", "verify"}},
    };

    bool aliasUsed = false;

    Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos) override
    {
        if (aliasUsed || command || pos == args.end()) return pos;
        auto arg = *pos;
        auto i = aliases.find(arg);
        if (i == aliases.end()) return pos;
        warn("'%s' is a deprecated alias for '%s'",
            arg, concatStringsSep(" ", i->second));
        pos = args.erase(pos);
        for (auto j = i->second.rbegin(); j != i->second.rend(); ++j)
            pos = args.insert(pos, *j);
        aliasUsed = true;
        return pos;
    }

    std::string description() override
    {
        return "a tool for reproducible and declarative configuration management";
    }

    std::string doc() override
    {
        return
          #include "nix.md"
          ;
    }

    // Plugins may add new subcommands.
    void pluginsInited() override
    {
        commands = RegisterCommand::getCommandsFor({});
    }
};

/* Render the help for the specified subcommand to stdout using
   lowdown. */
static void showHelp(std::vector<std::string> subcommand, MultiCommand & toplevel)
{
    auto mdName = subcommand.empty() ? "nix" : fmt("nix3-%s", concatStringsSep("-", subcommand));

    evalSettings.restrictEval = false;
    evalSettings.pureEval = false;
    EvalState state({}, openStore("dummy://"));

    auto vGenerateManpage = state.allocValue();
    state.eval(state.parseExprFromString(
        #include "generate-manpage.nix.gen.hh"
        , "/"), *vGenerateManpage);

    auto vUtils = state.allocValue();
    state.cacheFile(
        "/utils.nix", "/utils.nix",
        state.parseExprFromString(
            #include "utils.nix.gen.hh"
            , "/"),
        *vUtils);

    auto vArgs = state.allocValue();
    state.mkAttrs(*vArgs, 16);
    auto vJson = state.allocAttr(*vArgs, state.symbols.create("command"));
    mkString(*vJson, toplevel.toJSON().dump());
    vArgs->attrs->sort();

    auto vRes = state.allocValue();
    state.callFunction(*vGenerateManpage, *vArgs, *vRes, noPos);

    auto attr = vRes->attrs->get(state.symbols.create(mdName + ".md"));
    if (!attr)
        throw UsageError("Nix has no subcommand '%s'", concatStringsSep("", subcommand));

    auto markdown = state.forceString(*attr->value);

    RunPager pager;
    std::cout << renderMarkdownToTerminal(markdown) << "\n";
}

struct CmdHelp : Command
{
    std::vector<std::string> subcommand;

    CmdHelp()
    {
        expectArgs({
            .label = "subcommand",
            .handler = {&subcommand},
        });
    }

    std::string description() override
    {
        return "show help about `nix` or a particular subcommand";
    }

    std::string doc() override
    {
        return
          #include "help.md"
          ;
    }

    void run() override
    {
        assert(parent);
        MultiCommand * toplevel = parent;
        while (toplevel->parent) toplevel = toplevel->parent;
        showHelp(subcommand, *toplevel);
    }
};

static auto rCmdHelp = registerCommand<CmdHelp>("help");

void mainWrapped(int argc, char * * argv)
{
    savedArgv = argv;

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

    verbosity = lvlNotice;
    settings.verboseBuild = false;
    evalSettings.pureEval = true;

    setLogFormat("bar");

    Finally f([] { logger->stop(); });

    NixArgs args;

    if (argc == 2 && std::string(argv[1]) == "__dump-args") {
        std::cout << args.toJSON().dump() << "\n";
        return;
    }

    if (argc == 2 && std::string(argv[1]) == "__dump-builtins") {
        evalSettings.pureEval = false;
        EvalState state({}, openStore("dummy://"));
        auto res = nlohmann::json::object();
        auto builtins = state.baseEnv.values[0]->attrs;
        for (auto & builtin : *builtins) {
            auto b = nlohmann::json::object();
            if (!builtin.value->isPrimOp()) continue;
            auto primOp = builtin.value->primOp;
            if (!primOp->doc) continue;
            b["arity"] = primOp->arity;
            b["args"] = primOp->args;
            b["doc"] = trim(stripIndentation(primOp->doc));
            res[(std::string) builtin.name] = std::move(b);
        }
        std::cout << res.dump() << "\n";
        return;
    }

    Finally printCompletions([&]()
    {
        if (completions) {
            std::cout << (pathCompletions ? "filenames\n" : "no-filenames\n");
            for (auto & s : *completions)
                std::cout << s.completion << "\t" << s.description << "\n";
        }
    });

    try {
        args.parseCmdline(argvToStrings(argc, argv));
    } catch (HelpRequested &) {
        std::vector<std::string> subcommand;
        MultiCommand * command = &args;
        while (command) {
            if (command && command->command) {
                subcommand.push_back(command->command->first);
                command = dynamic_cast<MultiCommand *>(&*command->command->second);
            } else
                break;
        }
        showHelp(subcommand, args);
        return;
    } catch (UsageError &) {
        if (!completions) throw;
    }

    if (completions) return;

    if (args.showVersion) {
        printVersion(programName);
        return;
    }

    if (!args.command)
        throw UsageError("no subcommand specified");

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
        if (!settings.useSubstitutes.overridden)
            settings.useSubstitutes = false;
        if (!settings.tarballTtl.overridden)
            settings.tarballTtl = std::numeric_limits<unsigned int>::max();
        if (!fileTransferSettings.tries.overridden)
            fileTransferSettings.tries = 0;
        if (!fileTransferSettings.connectTimeout.overridden)
            fileTransferSettings.connectTimeout = 1;
    }

    if (args.refresh) {
        settings.tarballTtl = 0;
        settings.ttlNegativeNarInfoCache = 0;
        settings.ttlPositiveNarInfoCache = 0;
    }

    args.command->second->prepare();
    args.command->second->run();
}

}

int main(int argc, char * * argv)
{
    // Increase the default stack size for the evaluator and for
    // libstdc++'s std::regex.
    nix::setStackSize(64 * 1024 * 1024);

    return nix::handleExceptions(argv[0], [&]() {
        nix::mainWrapped(argc, argv);
    });
}

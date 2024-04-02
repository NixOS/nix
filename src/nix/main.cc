#include <algorithm>

#include "args/root.hh"
#include "current-process.hh"
#include "command.hh"
#include "common-args.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "globals.hh"
#include "legacy.hh"
#include "shared.hh"
#include "store-api.hh"
#include "filetransfer.hh"
#include "finally.hh"
#include "loggers.hh"
#include "markdown.hh"
#include "memory-input-accessor.hh"
#include "terminal.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex>

#include <nlohmann/json.hpp>

#if __linux__
# include "namespaces.hh"
#endif

extern std::string chrootHelperName;

void chrootHelper(int argc, char * * argv);

namespace nix {

static bool haveProxyEnvironmentVariables()
{
    static const std::vector<std::string> proxyVariables = {
        "http_proxy",
        "https_proxy",
        "ftp_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "FTP_PROXY"
    };
    for (auto & proxyVariable: proxyVariables) {
        if (getEnv(proxyVariable).has_value()) {
            return true;
        }
    }
    return false;
}

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

    if (haveProxyEnvironmentVariables()) return true;

    return false;
}

std::string programPath;

struct NixArgs : virtual MultiCommand, virtual MixCommonArgs, virtual RootArgs
{
    bool useNet = true;
    bool refresh = false;
    bool helpRequested = false;
    bool showVersion = false;

    NixArgs() : MultiCommand("", RegisterCommand::getCommandsFor({})), MixCommonArgs("nix")
    {
        categories.clear();
        categories[catHelp] = "Help commands";
        categories[Command::catDefault] = "Main commands";
        categories[catSecondary] = "Infrequently used commands";
        categories[catUtility] = "Utility/scripting commands";
        categories[catNixInstallation] = "Commands for upgrading or troubleshooting your Nix installation";

        addFlag({
            .longName = "help",
            .description = "Show usage information.",
            .category = miscCategory,
            .handler = {[this]() { this->helpRequested = true; }},
        });

        addFlag({
            .longName = "print-build-logs",
            .shortName = 'L',
            .description = "Print full build logs on standard error.",
            .category = loggingCategory,
            .handler = {[&]() { logger->setPrintBuildLogs(true); }},
            .experimentalFeature = Xp::NixCommand,
        });

        addFlag({
            .longName = "version",
            .description = "Show version information.",
            .category = miscCategory,
            .handler = {[&]() { showVersion = true; }},
        });

        addFlag({
            .longName = "offline",
            .aliases = {"no-net"}, // FIXME: remove
            .description = "Disable substituters and consider all previously downloaded files up-to-date.",
            .category = miscCategory,
            .handler = {[&]() { useNet = false; }},
            .experimentalFeature = Xp::NixCommand,
        });

        addFlag({
            .longName = "refresh",
            .description = "Consider all previously downloaded files out-of-date.",
            .category = miscCategory,
            .handler = {[&]() { refresh = true; }},
            .experimentalFeature = Xp::NixCommand,
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
        {"make-content-addressable", {"store", "make-content-addressed"}},
        {"optimise-store", {"store", "optimise"}},
        {"ping-store", {"store", "ping"}},
        {"sign-paths", {"store", "sign"}},
        {"show-derivation", {"derivation", "show"}},
        {"show-config", {"config", "show"}},
        {"to-base16", {"hash", "to-base16"}},
        {"to-base32", {"hash", "to-base32"}},
        {"to-base64", {"hash", "to-base64"}},
        {"verify", {"store", "verify"}},
        {"doctor", {"config", "check"}},
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

    std::string dumpCli()
    {
        auto res = nlohmann::json::object();

        res["args"] = toJSON();

        auto stores = nlohmann::json::object();
        for (auto & implem : *Implementations::registered) {
            auto storeConfig = implem.getConfig();
            auto storeName = storeConfig->name();
            auto & j = stores[storeName];
            j["doc"] = storeConfig->doc();
            j["settings"] = storeConfig->toJSON();
            j["experimentalFeature"] = storeConfig->experimentalFeature();
        }
        res["stores"] = std::move(stores);
        res["fetchers"] = fetchers::dumpRegisterInputSchemeInfo();

        return res.dump();
    }
};

/* Render the help for the specified subcommand to stdout using
   lowdown. */
static void showHelp(std::vector<std::string> subcommand, NixArgs & toplevel)
{
    auto mdName = subcommand.empty() ? "nix" : fmt("nix3-%s", concatStringsSep("-", subcommand));

    evalSettings.restrictEval = false;
    evalSettings.pureEval = false;
    EvalState state({}, openStore("dummy://"));

    auto vGenerateManpage = state.allocValue();
    state.eval(state.parseExprFromString(
        #include "generate-manpage.nix.gen.hh"
        , state.rootPath(CanonPath::root)), *vGenerateManpage);

    state.corepkgsFS->addFile(
        CanonPath("utils.nix"),
        #include "utils.nix.gen.hh"
        );

    state.corepkgsFS->addFile(
        CanonPath("/generate-settings.nix"),
        #include "generate-settings.nix.gen.hh"
        );

    state.corepkgsFS->addFile(
        CanonPath("/generate-store-info.nix"),
        #include "generate-store-info.nix.gen.hh"
        );

    auto vDump = state.allocValue();
    vDump->mkString(toplevel.dumpCli());

    auto vRes = state.allocValue();
    state.callFunction(*vGenerateManpage, state.getBuiltin("false"), *vRes, noPos);
    state.callFunction(*vRes, *vDump, *vRes, noPos);

    auto attr = vRes->attrs->get(state.symbols.create(mdName + ".md"));
    if (!attr)
        throw UsageError("Nix has no subcommand '%s'", concatStringsSep("", subcommand));

    auto markdown = state.forceString(*attr->value, noPos, "while evaluating the lowdown help text");

    RunPager pager;
    std::cout << renderMarkdownToTerminal(markdown) << "\n";
}

static NixArgs & getNixArgs(Command & cmd)
{
    return dynamic_cast<NixArgs &>(cmd.getRoot());
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

    Category category() override { return catHelp; }

    void run() override
    {
        assert(parent);
        MultiCommand * toplevel = parent;
        while (toplevel->parent) toplevel = toplevel->parent;
        showHelp(subcommand, getNixArgs(*this));
    }
};

static auto rCmdHelp = registerCommand<CmdHelp>("help");

struct CmdHelpStores : Command
{
    std::string description() override
    {
        return "show help about store types and their settings";
    }

    std::string doc() override
    {
        return
          #include "generated-doc/help-stores.md"
          ;
    }

    Category category() override { return catHelp; }

    void run() override
    {
        showHelp({"help-stores"}, getNixArgs(*this));
    }
};

static auto rCmdHelpStores = registerCommand<CmdHelpStores>("help-stores");

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

    #if __linux__
    if (isRootUser()) {
        try {
            saveMountNamespace();
            if (unshare(CLONE_NEWNS) == -1)
                throw SysError("setting up a private mount namespace");
        } catch (Error & e) { }
    }
    #endif

    Finally f([] { logger->stop(); });

    programPath = argv[0];
    auto programName = std::string(baseNameOf(programPath));

    if (argc > 1 && std::string_view(argv[1]) == "__build-remote") {
        programName = "build-remote";
        argv++; argc--;
    }

    {
        auto legacy = (*RegisterLegacyCommand::commands)[programName];
        if (legacy) return legacy(argc, argv);
    }

    evalSettings.pureEval = true;

    setLogFormat("bar");
    settings.verboseBuild = false;

    // If on a terminal, progress will be displayed via progress bars etc. (thus verbosity=notice)
    if (nix::isTTY()) {
        verbosity = lvlNotice;
    } else {
        verbosity = lvlInfo;
    }

    NixArgs args;

    if (argc == 2 && std::string(argv[1]) == "__dump-cli") {
        logger->cout(args.dumpCli());
        return;
    }

    if (argc == 2 && std::string(argv[1]) == "__dump-language") {
        experimentalFeatureSettings.experimentalFeatures = {
            Xp::Flakes,
            Xp::FetchClosure,
            Xp::DynamicDerivations,
            Xp::FetchTree,
        };
        evalSettings.pureEval = false;
        EvalState state({}, openStore("dummy://"));
        auto res = nlohmann::json::object();
        res["builtins"] = ({
            auto builtinsJson = nlohmann::json::object();
            auto builtins = state.baseEnv.values[0]->attrs;
            for (auto & builtin : *builtins) {
                auto b = nlohmann::json::object();
                if (!builtin.value->isPrimOp()) continue;
                auto primOp = builtin.value->primOp;
                if (!primOp->doc) continue;
                b["arity"] = primOp->arity;
                b["args"] = primOp->args;
                b["doc"] = trim(stripIndentation(primOp->doc));
                b["experimental-feature"] = primOp->experimentalFeature;
                builtinsJson[state.symbols[builtin.name]] = std::move(b);
            }
            std::move(builtinsJson);
        });
        res["constants"] = ({
            auto constantsJson = nlohmann::json::object();
            for (auto & [name, info] : state.constantInfos) {
                auto c = nlohmann::json::object();
                if (!info.doc) continue;
                c["doc"] = trim(stripIndentation(info.doc));
                c["type"] = showType(info.type, false);
                c["impure-only"] = info.impureOnly;
                constantsJson[name] = std::move(c);
            }
            std::move(constantsJson);
        });
        logger->cout("%s", res);
        return;
    }

    if (argc == 2 && std::string(argv[1]) == "__dump-xp-features") {
        logger->cout(documentExperimentalFeatures().dump());
        return;
    }

    Finally printCompletions([&]()
    {
        if (args.completions) {
            switch (args.completions->type) {
            case Completions::Type::Normal:
                logger->cout("normal"); break;
            case Completions::Type::Filenames:
                logger->cout("filenames"); break;
            case Completions::Type::Attrs:
                logger->cout("attrs"); break;
            }
            for (auto & s : args.completions->completions)
                logger->cout(s.completion + "\t" + trim(s.description));
        }
    });

    try {
        auto isNixCommand = std::regex_search(programName, std::regex("nix$"));
        auto allowShebang = isNixCommand && argc > 1;
        args.parseCmdline(argvToStrings(argc, argv),allowShebang);
    } catch (UsageError &) {
        if (!args.helpRequested && !args.completions) throw;
    }

    if (args.helpRequested) {
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
    }

    if (args.completions) return;

    if (args.showVersion) {
        printVersion(programName);
        return;
    }

    if (!args.command)
        throw UsageError("no subcommand specified");

    experimentalFeatureSettings.require(
        args.command->second->experimentalFeature());

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

    if (args.command->second->forceImpureByDefault() && !evalSettings.pureEval.overridden) {
        evalSettings.pureEval = false;
    }
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

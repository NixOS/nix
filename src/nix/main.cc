#include "args/root.hh"
#include "current-process.hh"
#include "command.hh"
#include "common-args.hh"
#include "eval-gc.hh"
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
#include "memory-source-accessor.hh"
#include "terminal.hh"
#include "users.hh"
#include "network-proxy.hh"
#include "eval-cache.hh"
#include "flake/flake.hh"

#include <sys/types.h>
#include <regex>
#include <nlohmann/json.hpp>

#ifndef _WIN32
# include <sys/socket.h>
# include <ifaddrs.h>
# include <netdb.h>
# include <netinet/in.h>
#endif

#if __linux__
# include "namespaces.hh"
#endif

#ifndef _WIN32
extern std::string chrootHelperName;

void chrootHelper(int argc, char * * argv);
#endif

#include "strings.hh"

namespace nix {

enum struct AliasStatus {
    /** Aliases that don't go away */
    AcceptedShorthand,
    /** Aliases that will go away */
    Deprecated,
};

/** An alias, except for the original syntax, which is in the map key. */
struct AliasInfo {
    AliasStatus status;
    std::vector<std::string> replacement;
};

/* Check if we have a non-loopback/link-local network interface. */
static bool haveInternet()
{
#ifndef _WIN32
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

    if (haveNetworkProxyConnection()) return true;

    return false;
#else
    // TODO implement on Windows
    return true;
#endif
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

    std::map<std::string, AliasInfo> aliases = {
        {"add-to-store", { AliasStatus::Deprecated, {"store", "add-path"}}},
        {"cat-nar", { AliasStatus::Deprecated, {"nar", "cat"}}},
        {"cat-store", { AliasStatus::Deprecated, {"store", "cat"}}},
        {"copy-sigs", { AliasStatus::Deprecated, {"store", "copy-sigs"}}},
        {"dev-shell", { AliasStatus::Deprecated, {"develop"}}},
        {"diff-closures", { AliasStatus::Deprecated, {"store", "diff-closures"}}},
        {"dump-path", { AliasStatus::Deprecated, {"store", "dump-path"}}},
        {"hash-file", { AliasStatus::Deprecated, {"hash", "file"}}},
        {"hash-path", { AliasStatus::Deprecated, {"hash", "path"}}},
        {"ls-nar", { AliasStatus::Deprecated, {"nar", "ls"}}},
        {"ls-store", { AliasStatus::Deprecated, {"store", "ls"}}},
        {"make-content-addressable", { AliasStatus::Deprecated, {"store", "make-content-addressed"}}},
        {"optimise-store", { AliasStatus::Deprecated, {"store", "optimise"}}},
        {"ping-store", { AliasStatus::Deprecated, {"store", "ping"}}},
        {"sign-paths", { AliasStatus::Deprecated, {"store", "sign"}}},
        {"shell", { AliasStatus::AcceptedShorthand, {"env", "shell"}}},
        {"show-derivation", { AliasStatus::Deprecated, {"derivation", "show"}}},
        {"show-config", { AliasStatus::Deprecated, {"config", "show"}}},
        {"to-base16", { AliasStatus::Deprecated, {"hash", "to-base16"}}},
        {"to-base32", { AliasStatus::Deprecated, {"hash", "to-base32"}}},
        {"to-base64", { AliasStatus::Deprecated, {"hash", "to-base64"}}},
        {"verify", { AliasStatus::Deprecated, {"store", "verify"}}},
        {"doctor", { AliasStatus::Deprecated, {"config", "check"}}},
    };

    bool aliasUsed = false;

    Strings::iterator rewriteArgs(Strings & args, Strings::iterator pos) override
    {
        if (aliasUsed || command || pos == args.end()) return pos;
        auto arg = *pos;
        auto i = aliases.find(arg);
        if (i == aliases.end()) return pos;
        auto & info = i->second;
        if (info.status == AliasStatus::Deprecated) {
            warn("'%s' is a deprecated alias for '%s'",
                arg, concatStringsSep(" ", info.replacement));
        }
        pos = args.erase(pos);
        for (auto j = info.replacement.rbegin(); j != info.replacement.rend(); ++j)
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
    EvalState state({}, openStore("dummy://"), fetchSettings, evalSettings);

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

    auto attr = vRes->attrs()->get(state.symbols.create(mdName + ".md"));
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
          #include "help-stores.md.gen.hh"
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
#ifndef _WIN32
    if (argc > 0 && argv[0] == chrootHelperName) {
        chrootHelper(argc, argv);
        return;
    }
#endif

    initNix();
    initGC();
    flake::initLib(flakeSettings);

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
    auto extensionPos = programName.find_last_of(".");
    if (extensionPos != std::string::npos)
        programName.erase(extensionPos);

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
        EvalState state({}, openStore("dummy://"), fetchSettings, evalSettings);
        auto builtinsJson = nlohmann::json::object();
        for (auto & builtin : *state.baseEnv.values[0]->attrs()) {
            auto b = nlohmann::json::object();
            if (!builtin.value->isPrimOp()) continue;
            auto primOp = builtin.value->primOp();
            if (!primOp->doc) continue;
            b["args"] = primOp->args;
            b["doc"] = trim(stripIndentation(primOp->doc));
            if (primOp->experimentalFeature)
                b["experimental-feature"] = primOp->experimentalFeature;
            builtinsJson.emplace(state.symbols[builtin.name], std::move(b));
        }
        for (auto & [name, info] : state.constantInfos) {
            auto b = nlohmann::json::object();
            if (!info.doc) continue;
            b["doc"] = trim(stripIndentation(info.doc));
            b["type"] = showType(info.type, false);
            if (info.impureOnly)
                b["impure-only"] = true;
            builtinsJson[name] = std::move(b);
        }
        logger->cout("%s", builtinsJson);
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

    try {
        args.command->second->run();
    } catch (eval_cache::CachedEvalError & e) {
        /* Evaluate the original attribute that resulted in this
           cached error so that we can show the original error to the
           user. */
        e.force();
    }
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

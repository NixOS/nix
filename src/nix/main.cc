#include "nix/util/args/root.hh"
#include "nix/util/current-process.hh"
#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/globals.hh"
#include "nix/cmd/legacy.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-open.hh"
#include "nix/store/store-registration.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/finally.hh"
#include "nix/main/loggers.hh"
#include "nix/cmd/markdown.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/terminal.hh"
#include "nix/util/users.hh"
#include "nix/cmd/network-proxy.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/flake/flake.hh"
#include "nix/flake/settings.hh"
#include "nix/util/json-utils.hh"

#include "self-exe.hh"
#include "crash-handler.hh"
#include "cli-config-private.hh"

#include <sys/types.h>
#include <regex>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#  include <sys/socket.h>
#  include <ifaddrs.h>
#  include <netdb.h>
#  include <netinet/in.h>
#endif

#ifdef __linux__
#  include "nix/util/linux-namespaces.hh"
#endif

#ifndef _WIN32
extern std::string chrootHelperName;

void chrootHelper(int argc, char ** argv);
#endif

#include "nix/util/strings.hh"

namespace nix {

/* Check if we have a non-loopback/link-local network interface. */
static bool haveInternet()
{
#ifndef _WIN32
    struct ifaddrs * addrs;

    if (getifaddrs(&addrs))
        return true;

    Finally free([&]() { freeifaddrs(addrs); });

    for (auto i = addrs; i; i = i->ifa_next) {
        if (!i->ifa_addr)
            continue;
        if (i->ifa_addr->sa_family == AF_INET) {
            if (ntohl(((sockaddr_in *) i->ifa_addr)->sin_addr.s_addr) != INADDR_LOOPBACK) {
                return true;
            }
        } else if (i->ifa_addr->sa_family == AF_INET6) {
            if (!IN6_IS_ADDR_LOOPBACK(&((sockaddr_in6 *) i->ifa_addr)->sin6_addr)
                && !IN6_IS_ADDR_LINKLOCAL(&((sockaddr_in6 *) i->ifa_addr)->sin6_addr))
                return true;
        }
    }

    if (haveNetworkProxyConnection())
        return true;

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

    NixArgs()
        : MultiCommand("", RegisterCommand::getCommandsFor({}))
        , MixCommonArgs("nix")
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

        aliases = {
            {"add-to-store", {AliasStatus::Deprecated, {"store", "add-path"}}},
            {"cat-nar", {AliasStatus::Deprecated, {"nar", "cat"}}},
            {"cat-store", {AliasStatus::Deprecated, {"store", "cat"}}},
            {"copy-sigs", {AliasStatus::Deprecated, {"store", "copy-sigs"}}},
            {"dev-shell", {AliasStatus::Deprecated, {"develop"}}},
            {"diff-closures", {AliasStatus::Deprecated, {"store", "diff-closures"}}},
            {"dump-path", {AliasStatus::Deprecated, {"store", "dump-path"}}},
            {"hash-file", {AliasStatus::Deprecated, {"hash", "file"}}},
            {"hash-path", {AliasStatus::Deprecated, {"hash", "path"}}},
            {"ls-nar", {AliasStatus::Deprecated, {"nar", "ls"}}},
            {"ls-store", {AliasStatus::Deprecated, {"store", "ls"}}},
            {"make-content-addressable", {AliasStatus::Deprecated, {"store", "make-content-addressed"}}},
            {"optimise-store", {AliasStatus::Deprecated, {"store", "optimise"}}},
            {"ping-store", {AliasStatus::Deprecated, {"store", "info"}}},
            {"sign-paths", {AliasStatus::Deprecated, {"store", "sign"}}},
            {"shell", {AliasStatus::AcceptedShorthand, {"env", "shell"}}},
            {"show-derivation", {AliasStatus::Deprecated, {"derivation", "show"}}},
            {"show-config", {AliasStatus::Deprecated, {"config", "show"}}},
            {"to-base16", {AliasStatus::Deprecated, {"hash", "to-base16"}}},
            {"to-base32", {AliasStatus::Deprecated, {"hash", "to-base32"}}},
            {"to-base64", {AliasStatus::Deprecated, {"hash", "to-base64"}}},
            {"verify", {AliasStatus::Deprecated, {"store", "verify"}}},
            {"doctor", {AliasStatus::Deprecated, {"config", "check"}}},
        };
    };

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
        for (auto & [storeName, implem] : Implementations::registered()) {
            auto & j = stores[storeName];
            j["doc"] = implem.doc;
            j["uri-schemes"] = implem.uriSchemes;
            j["settings"] = implem.getConfig()->toJSON();
            j["experimentalFeature"] = implem.experimentalFeature;
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
    // Check for aliases if subcommand has exactly one element
    if (subcommand.size() == 1) {
        auto alias = toplevel.aliases.find(subcommand[0]);
        if (alias != toplevel.aliases.end()) {
            subcommand = alias->second.replacement;
        }
    }

    auto mdName = subcommand.empty() ? "nix" : fmt("nix3-%s", concatStringsSep("-", subcommand));

    evalSettings.restrictEval = true;
    evalSettings.pureEval = true;
    EvalState state({}, openStore("dummy://"), fetchSettings, evalSettings);

    auto vGenerateManpage = state.allocValue();
    state.eval(
        state.parseExprFromString(
#include "generate-manpage.nix.gen.hh"
            , state.rootPath(CanonPath::root)),
        *vGenerateManpage);

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
    Value * args[]{&state.getBuiltin("false"), vDump};
    state.callFunction(*vGenerateManpage, args, *vRes, noPos);

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

    Category category() override
    {
        return catHelp;
    }

    void run() override
    {
        assert(parent);
        MultiCommand * toplevel = parent;
        while (toplevel->parent)
            toplevel = toplevel->parent;
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

    Category category() override
    {
        return catHelp;
    }

    void run() override
    {
        showHelp({"help-stores"}, getNixArgs(*this));
    }
};

static auto rCmdHelpStores = registerCommand<CmdHelpStores>("help-stores");

void mainWrapped(int argc, char ** argv)
{
    savedArgv = argv;

    registerCrashHandler();

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
    flakeSettings.configureEvalSettings(evalSettings);

    /* Set the build hook location

       For builds we perform a self-invocation, so Nix has to be
       self-aware. That is, it has to know where it is installed. We
       don't think it's sentient.
     */
    settings.buildHook.setDefault(
        Strings{
            getNixBin({}).string(),
            "__build-remote",
        });

#ifdef __linux__
    if (isRootUser()) {
        try {
            saveMountNamespace();
            if (unshare(CLONE_NEWNS) == -1)
                throw SysError("setting up a private mount namespace");
        } catch (Error & e) {
        }
    }
#endif

    programPath = argv[0];
    auto programName = std::string(baseNameOf(programPath));
    auto extensionPos = programName.find_last_of(".");
    if (extensionPos != std::string::npos)
        programName.erase(extensionPos);

    if (argc > 1 && std::string_view(argv[1]) == "__build-remote") {
        programName = "build-remote";
        argv++;
        argc--;
    }

    {
        auto legacy = RegisterLegacyCommand::commands()[programName];
        if (legacy)
            return legacy(argc, argv);
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
        for (auto & builtinPtr : state.getBuiltins().attrs()->lexicographicOrder(state.symbols)) {
            auto & builtin = *builtinPtr;
            auto b = nlohmann::json::object();
            if (!builtin.value->isPrimOp())
                continue;
            auto primOp = builtin.value->primOp();
            if (!primOp->doc)
                continue;
            b["args"] = primOp->args;
            b["doc"] = trim(stripIndentation(primOp->doc));
            if (primOp->experimentalFeature)
                b["experimental-feature"] = primOp->experimentalFeature;
            builtinsJson.emplace(state.symbols[builtin.name], std::move(b));
        }
        for (auto & [name, info] : state.constantInfos) {
            auto b = nlohmann::json::object();
            if (!info.doc)
                continue;
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

    Finally printCompletions([&]() {
        if (args.completions) {
            switch (args.completions->type) {
            case Completions::Type::Normal:
                logger->cout("normal");
                break;
            case Completions::Type::Filenames:
                logger->cout("filenames");
                break;
            case Completions::Type::Attrs:
                logger->cout("attrs");
                break;
            }
            for (auto & s : args.completions->completions)
                logger->cout(s.completion + "\t" + trim(s.description));
        }
    });

    try {
        auto isNixCommand = std::regex_search(programName, std::regex("nix$"));
        auto allowShebang = isNixCommand && argc > 1;
        args.parseCmdline(argvToStrings(argc, argv), allowShebang);
    } catch (UsageError &) {
        if (!args.helpRequested && !args.completions)
            throw;
    }

    applyJSONLogger();

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

    if (args.completions)
        return;

    if (args.showVersion) {
        printVersion(programName);
        return;
    }

    if (!args.command)
        throw UsageError("no subcommand specified");

    experimentalFeatureSettings.require(args.command->second->experimentalFeature());

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

} // namespace nix

int main(int argc, char ** argv)
{
    // The CLI has a more detailed version than the libraries; see nixVersion.
    nix::nixVersion = NIX_CLI_VERSION;
#ifndef _WIN32
    // Increase the default stack size for the evaluator and for
    // libstdc++'s std::regex.
    nix::setStackSize(64 * 1024 * 1024);
#endif

    return nix::handleExceptions(argv[0], [&]() { nix::mainWrapped(argc, argv); });
}

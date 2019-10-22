#include "shared.hh"
#include "globals.hh"
#include "download.hh"
#include "store-api.hh"
#include "legacy.hh"

#include <fcntl.h>
#include <regex>
#include <pwd.h>

using namespace nix;

typedef std::map<string,string> Channels;

static Channels channels;
static Path channelsList;

// Reads the list of channels.
static void readChannels()
{
    if (!pathExists(channelsList)) return;
    auto channelsFile = readFile(channelsList);

    for (const auto & line : tokenizeString<std::vector<string>>(channelsFile, "\n")) {
        chomp(line);
        if (std::regex_search(line, std::regex("^\\s*\\#")))
            continue;
        auto split = tokenizeString<std::vector<string>>(line, " ");
        auto url = std::regex_replace(split[0], std::regex("/*$"), "");
        auto name = split.size() > 1 ? split[1] : baseNameOf(url);
        channels[name] = url;
    }
}

// Writes the list of channels.
static void writeChannels()
{
    auto channelsFD = AutoCloseFD{open(channelsList.c_str(), O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, 0644)};
    if (!channelsFD)
        throw SysError(format("opening '%1%' for writing") % channelsList);
    for (const auto & channel : channels)
        writeFull(channelsFD.get(), channel.second + " " + channel.first + "\n");
}

// Adds a channel.
static void addChannel(const string & url, const string & name)
{
    if (!regex_search(url, std::regex("^(file|http|https)://")))
        throw Error(format("invalid channel URL '%1%'") % url);
    if (!regex_search(name, std::regex("^[a-zA-Z0-9_][a-zA-Z0-9_\\.-]*$")))
        throw Error(format("invalid channel identifier '%1%'") % name);
    readChannels();
    channels[name] = url;
    writeChannels();
}

static Path profile;

// Remove a channel.
static void removeChannel(const string & name)
{
    readChannels();
    channels.erase(name);
    writeChannels();

    runProgram(settings.nixBinDir + "/nix-env", true, { "--profile", profile, "--uninstall", name });
}

static Path nixDefExpr;

// Fetch Nix expressions and binary cache URLs from the subscribed channels.
static void update(const StringSet & channelNames)
{
    readChannels();

    auto store = openStore();

    // Download each channel.
    Strings exprs;
    for (const auto & channel : channels) {
        auto name = channel.first;
        auto url = channel.second;
        if (!(channelNames.empty() || channelNames.count(name)))
            continue;

        // We want to download the url to a file to see if it's a tarball while also checking if we
        // got redirected in the process, so that we can grab the various parts of a nix channel
        // definition from a consistent location if the redirect changes mid-download.
        CachedDownloadRequest request(url);
        request.ttl = 0;
        auto dl = getDownloader();
        auto result = dl->downloadCached(store, request);
        auto filename = result.path;
        url = chomp(result.effectiveUri);

        // If the URL contains a version number, append it to the name
        // attribute (so that "nix-env -q" on the channels profile
        // shows something useful).
        auto cname = name;
        std::smatch match;
        auto urlBase = baseNameOf(url);
        if (std::regex_search(urlBase, match, std::regex("(-\\d.*)$"))) {
            cname = cname + (string) match[1];
        }

        std::string extraAttrs;

        bool unpacked = false;
        if (std::regex_search(filename, std::regex("\\.tar\\.(gz|bz2|xz)$"))) {
            runProgram(settings.nixBinDir + "/nix-build", false, { "--no-out-link", "--expr", "import <nix/unpack-channel.nix> "
                        "{ name = \"" + cname + "\"; channelName = \"" + name + "\"; src = builtins.storePath \"" + filename + "\"; }" });
            unpacked = true;
        }

        if (!unpacked) {
            // Download the channel tarball.
            try {
                filename = dl->downloadCached(store, CachedDownloadRequest(url + "/nixexprs.tar.xz")).path;
            } catch (DownloadError & e) {
                filename = dl->downloadCached(store, CachedDownloadRequest(url + "/nixexprs.tar.bz2")).path;
            }
            chomp(filename);
        }

        // Regardless of where it came from, add the expression representing this channel to accumulated expression
        exprs.push_back("f: f { name = \"" + cname + "\"; channelName = \"" + name + "\"; src = builtins.storePath \"" + filename + "\"; " + extraAttrs + " }");
    }

    // Unpack the channel tarballs into the Nix store and install them
    // into the channels profile.
    std::cerr << "unpacking channels...\n";
    Strings envArgs{ "--profile", profile, "--file", "<nix/unpack-channel.nix>", "--install", "--from-expression" };
    for (auto & expr : exprs)
        envArgs.push_back(std::move(expr));
    envArgs.push_back("--quiet");
    runProgram(settings.nixBinDir + "/nix-env", false, envArgs);

    // Make the channels appear in nix-env.
    struct stat st;
    if (lstat(nixDefExpr.c_str(), &st) == 0) {
        if (S_ISLNK(st.st_mode))
            // old-skool ~/.nix-defexpr
            if (unlink(nixDefExpr.c_str()) == -1)
                throw SysError(format("unlinking %1%") % nixDefExpr);
    } else if (errno != ENOENT) {
        throw SysError(format("getting status of %1%") % nixDefExpr);
    }
    createDirs(nixDefExpr);
    auto channelLink = nixDefExpr + "/channels";
    replaceSymlink(profile, channelLink);
}

static int _main(int argc, char ** argv)
{
    {
        // Figure out the name of the `.nix-channels' file to use
        auto home = getHome();
        channelsList = home + "/.nix-channels";
        nixDefExpr = home + "/.nix-defexpr";

        // Figure out the name of the channels profile.
        profile = fmt("%s/profiles/per-user/%s/channels", settings.nixStateDir, getUserName());

        enum {
            cNone,
            cAdd,
            cRemove,
            cList,
            cUpdate,
            cRollback
        } cmd = cNone;
        std::vector<string> args;
        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help") {
                showManPage("nix-channel");
            } else if (*arg == "--version") {
                printVersion("nix-channel");
            } else if (*arg == "--add") {
                cmd = cAdd;
            } else if (*arg == "--remove") {
                cmd = cRemove;
            } else if (*arg == "--list") {
                cmd = cList;
            } else if (*arg == "--update") {
                cmd = cUpdate;
            } else if (*arg == "--rollback") {
                cmd = cRollback;
            } else {
                args.push_back(std::move(*arg));
            }
            return true;
        });

        initPlugins();

        switch (cmd) {
            case cNone:
                throw UsageError("no command specified");
            case cAdd:
                if (args.size() < 1 || args.size() > 2)
                    throw UsageError("'--add' requires one or two arguments");
                {
                auto url = args[0];
                std::string name;
                if (args.size() == 2) {
                    name = args[1];
                } else {
                    name = baseNameOf(url);
                    name = std::regex_replace(name, std::regex("-unstable$"), "");
                    name = std::regex_replace(name, std::regex("-stable$"), "");
                }
                addChannel(url, name);
                }
                break;
            case cRemove:
                if (args.size() != 1)
                    throw UsageError("'--remove' requires one argument");
                removeChannel(args[0]);
                break;
            case cList:
                if (!args.empty())
                    throw UsageError("'--list' expects no arguments");
                readChannels();
                for (const auto & channel : channels)
                    std::cout << channel.first << ' ' << channel.second << '\n';
                break;
            case cUpdate:
                update(StringSet(args.begin(), args.end()));
                break;
            case cRollback:
                if (args.size() > 1)
                    throw UsageError("'--rollback' has at most one argument");
                Strings envArgs{"--profile", profile};
                if (args.size() == 1) {
                    envArgs.push_back("--switch-generation");
                    envArgs.push_back(args[0]);
                } else {
                    envArgs.push_back("--rollback");
                }
                runProgram(settings.nixBinDir + "/nix-env", false, envArgs);
                break;
        }

        return 0;
    }
}

static RegisterLegacyCommand s1("nix-channel", _main);

#include "shared.hh"
#include "globals.hh"
#include "download.hh"
#include <fcntl.h>
#include <regex>
#include "store-api.hh"
#include <pwd.h>

using namespace nix;

typedef std::map<string,string> Channels;

static auto channels = Channels{};
static auto channelsList = Path{};

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
        throw SysError(format("opening ‘%1%’ for writing") % channelsList);
    for (const auto & channel : channels)
        writeFull(channelsFD.get(), channel.second + " " + channel.first + "\n");
}

// Adds a channel.
static void addChannel(const string & url, const string & name)
{
    if (!regex_search(url, std::regex("^(file|http|https)://")))
        throw Error(format("invalid channel URL ‘%1%’") % url);
    if (!regex_search(name, std::regex("^[a-zA-Z0-9_][a-zA-Z0-9_\\.-]*$")))
        throw Error(format("invalid channel identifier ‘%1%’") % name);
    readChannels();
    channels[name] = url;
    writeChannels();
}

static auto profile = Path{};

// Remove a channel.
static void removeChannel(const string & name)
{
    readChannels();
    channels.erase(name);
    writeChannels();

    runProgram(settings.nixBinDir + "/nix-env", true, { "--profile", profile, "--uninstall", name });
}

static auto nixDefExpr = Path{};

// Fetch Nix expressions and binary cache URLs from the subscribed channels.
static void update(const StringSet & channelNames)
{
    readChannels();

    auto store = openStore();

    // Download each channel.
    auto exprs = Strings{};
    for (const auto & channel : channels) {
        if (!channelNames.empty() && channelNames.find(channel.first) != channelNames.end())
            continue;
        auto name = channel.first;
        auto url = channel.second;

        // We want to download the url to a file to see if it's a tarball while also checking if we
        // got redirected in the process, so that we can grab the various parts of a nix channel
        // definition from a consistent location if the redirect changes mid-download.
        auto effectiveUrl = string{};
        auto dl = makeDownloader();
        auto filename = dl->downloadCached(store, url, false, effectiveUrl);
        url = chomp(std::move(effectiveUrl));

        // If the URL contains a version number, append it to the name
        // attribute (so that "nix-env -q" on the channels profile
        // shows something useful).
        auto cname = name;
        std::smatch match;
        auto urlBase = baseNameOf(url);
        if (std::regex_search(urlBase, match, std::regex("(-\\d.*)$"))) {
            cname = cname + (string) match[1];
        }

        auto extraAttrs = string{};

        auto unpacked = false;
        if (std::regex_search(filename, std::regex("\\.tar\\.(gz|bz2|xz)$"))) {
            try {
                runProgram(settings.nixBinDir + "/nix-build", false, { "--no-out-link", "--expr", "import <nix/unpack-channel.nix> "
                            "{ name = \"" + cname + "\"; channelName = \"" + name + "\"; src = builtins.storePath \"" + filename + "\"; }" });
                unpacked = true;
            } catch (ExecError & e) {
            }
        }

        if (!unpacked) {
            // The URL doesn't unpack directly, so let's try treating it like a full channel folder with files in it
            // Check if the channel advertises a binary cache.
            DownloadOptions opts;
            opts.showProgress = DownloadOptions::no;
            try {
                auto dlRes = dl->download(url + "/binary-cache-url", opts);
                extraAttrs = "binaryCacheURL = \"" + *dlRes.data + "\";";
            } catch (DownloadError & e) {
            }

            // Download the channel tarball.
            auto fullURL = url + "/nixexprs.tar.xz";
            try {
                filename = dl->downloadCached(store, fullURL, false);
            } catch (DownloadError & e) {
                fullURL = url + "/nixexprs.tar.bz2";
                filename = dl->downloadCached(store, fullURL, false);
            }
            chomp(filename);
        }

        // Regardless of where it came from, add the expression representing this channel to accumulated expression
        exprs.push_back("f: f { name = \"" + cname + "\"; channelName = \"" + name + "\"; src = builtins.storePath \"" + filename + "\"; " + extraAttrs + " }");
    }

    // Unpack the channel tarballs into the Nix store and install them
    // into the channels profile.
    std::cerr << "unpacking channels...\n";
    auto envArgs = Strings{ "--profile", profile, "--file", "<nix/unpack-channel.nix>", "--install", "--from-expression" };
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

int main(int argc, char ** argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();

        // Turn on caching in nix-prefetch-url.
        auto channelCache = settings.nixStateDir + "/channel-cache";
        createDirs(channelCache);
        setenv("NIX_DOWNLOAD_CACHE", channelCache.c_str(), 1);

        // Figure out the name of the `.nix-channels' file to use
        auto home = getEnv("HOME");
        if (home.empty())
            throw Error("$HOME not set");
        channelsList = home + "/.nix-channels";
        nixDefExpr = home + "/.nix-defexpr";

        // Figure out the name of the channels profile.
        auto name = string{};
        auto pw = getpwuid(getuid());
        if (!pw)
            name = getEnv("USER", "");
        else
            name = pw->pw_name;
        if (name.empty())
            throw Error("cannot figure out user name");
        profile = settings.nixStateDir + "/profiles/per-user/" + name + "/channels";
        createDirs(dirOf(profile));

        enum {
            cNone,
            cAdd,
            cRemove,
            cList,
            cUpdate,
            cRollback
        } cmd = cNone;
        auto args = std::vector<string>{};
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
        switch (cmd) {
            case cNone:
                throw UsageError("no command specified");
            case cAdd:
                if (args.size() < 1 || args.size() > 2)
                    throw UsageError("‘--add’ requires one or two arguments");
                {
                auto url = args[0];
                auto name = string{};
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
                    throw UsageError("‘--remove’ requires one argument");
                removeChannel(args[0]);
                break;
            case cList:
                if (!args.empty())
                    throw UsageError("‘--list’ expects no arguments");
                readChannels();
                for (const auto & channel : channels)
                    std::cout << channel.first << ' ' << channel.second << '\n';
                break;
            case cUpdate:
                update(StringSet(args.begin(), args.end()));
                break;
            case cRollback:
                if (args.size() > 1)
                    throw UsageError("‘--rollback’ has at most one argument");
                auto envArgs = Strings{"--profile", profile};
                if (args.size() == 1) {
                    envArgs.push_back("--switch-generation");
                    envArgs.push_back(args[0]);
                } else {
                    envArgs.push_back("--rollback");
                }
                runProgram(settings.nixBinDir + "/nix-env", false, envArgs);
                break;
        }
    });
}

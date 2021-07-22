#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "filetransfer.hh"
#include "finally.hh"
#include "progress-bar.hh"
#include "tarfile.hh"
#include "attr-path.hh"
#include "eval-inline.hh"
#include "legacy.hh"

#include <nlohmann/json.hpp>

using namespace nix;

/* If ‘url’ starts with ‘mirror://’, then resolve it using the list of
   mirrors defined in Nixpkgs. */
string resolveMirrorUrl(EvalState & state, string url)
{
    if (url.substr(0, 9) != "mirror://") return url;

    std::string s(url, 9);
    auto p = s.find('/');
    if (p == std::string::npos) throw Error("invalid mirror URL '%s'", url);
    std::string mirrorName(s, 0, p);

    Value vMirrors;
    // FIXME: use nixpkgs flake
    state.eval(state.parseExprFromString("import <nixpkgs/pkgs/build-support/fetchurl/mirrors.nix>", "."), vMirrors);
    state.forceAttrs(vMirrors);

    auto mirrorList = vMirrors.attrs->find(state.symbols.create(mirrorName));
    if (mirrorList == vMirrors.attrs->end())
        throw Error("unknown mirror name '%s'", mirrorName);
    state.forceList(*mirrorList->value);

    if (mirrorList->value->listSize() < 1)
        throw Error("mirror URL '%s' did not expand to anything", url);

    auto mirror = state.forceString(*mirrorList->value->listElems()[0]);
    return mirror + (hasSuffix(mirror, "/") ? "" : "/") + string(s, p + 1);
}

std::tuple<StorePath, Hash> prefetchFile(
    ref<Store> store,
    std::string_view url,
    std::optional<std::string> name,
    HashType hashType,
    std::optional<Hash> expectedHash,
    bool unpack,
    bool executable)
{
    auto ingestionMethod = unpack || executable ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat;

    /* Figure out a name in the Nix store. */
    if (!name) {
        name = baseNameOf(url);
        if (name->empty())
            throw Error("cannot figure out file name for '%s'", url);
    }

    std::optional<StorePath> storePath;
    std::optional<Hash> hash;

    /* If an expected hash is given, the file may already exist in
       the store. */
    if (expectedHash) {
        hashType = expectedHash->type;
        storePath = store->makeFixedOutputPath(ingestionMethod, *expectedHash, *name);
        if (store->isValidPath(*storePath))
            hash = expectedHash;
        else
            storePath.reset();
    }

    if (!storePath) {

        AutoDelete tmpDir(createTempDir(), true);
        Path tmpFile = (Path) tmpDir + "/tmp";

        /* Download the file. */
        {
            auto mode = 0600;
            if (executable)
                mode = 0700;

            AutoCloseFD fd = open(tmpFile.c_str(), O_WRONLY | O_CREAT | O_EXCL, mode);
            if (!fd) throw SysError("creating temporary file '%s'", tmpFile);

            FdSink sink(fd.get());

            FileTransferRequest req(url);
            req.decompress = false;
            getFileTransfer()->download(std::move(req), sink);
        }

        /* Optionally unpack the file. */
        if (unpack) {
            Activity act(*logger, lvlChatty, actUnknown,
                fmt("unpacking '%s'", url));
            Path unpacked = (Path) tmpDir + "/unpacked";
            createDirs(unpacked);
            unpackTarfile(tmpFile, unpacked);

            /* If the archive unpacks to a single file/directory, then use
               that as the top-level. */
            auto entries = readDirectory(unpacked);
            if (entries.size() == 1)
                tmpFile = unpacked + "/" + entries[0].name;
            else
                tmpFile = unpacked;
        }

        Activity act(*logger, lvlChatty, actUnknown,
            fmt("adding '%s' to the store", url));

        auto info = store->addToStoreSlow(*name, tmpFile, ingestionMethod, hashType, expectedHash);
        storePath = info.path;
        assert(info.ca);
        hash = getContentAddressHash(*info.ca);
    }

    return {storePath.value(), hash.value()};
}

static int main_nix_prefetch_url(int argc, char * * argv)
{
    {
        HashType ht = htSHA256;
        std::vector<string> args;
        bool printPath = getEnv("PRINT_PATH") == "1";
        bool fromExpr = false;
        string attrPath;
        bool unpack = false;
        bool executable = false;
        std::optional<std::string> name;

        struct MyArgs : LegacyArgs, MixEvalArgs
        {
            using LegacyArgs::LegacyArgs;
        };

        MyArgs myArgs(std::string(baseNameOf(argv[0])), [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-prefetch-url");
            else if (*arg == "--version")
                printVersion("nix-prefetch-url");
            else if (*arg == "--type") {
                string s = getArg(*arg, arg, end);
                ht = parseHashType(s);
            }
            else if (*arg == "--print-path")
                printPath = true;
            else if (*arg == "--attr" || *arg == "-A") {
                fromExpr = true;
                attrPath = getArg(*arg, arg, end);
            }
            else if (*arg == "--unpack")
                unpack = true;
            else if (*arg == "--executable")
                executable = true;
            else if (*arg == "--name")
                name = getArg(*arg, arg, end);
            else if (*arg != "" && arg->at(0) == '-')
                return false;
            else
                args.push_back(*arg);
            return true;
        });

        myArgs.parseCmdline(argvToStrings(argc, argv));

        if (args.size() > 2)
            throw UsageError("too many arguments");

        Finally f([]() { stopProgressBar(); });

        if (isatty(STDERR_FILENO))
          startProgressBar();

        auto store = openStore();
        auto state = std::make_unique<EvalState>(myArgs.searchPath, store);

        Bindings & autoArgs = *myArgs.getAutoArgs(*state);

        /* If -A is given, get the URL from the specified Nix
           expression. */
        string url;
        if (!fromExpr) {
            if (args.empty())
                throw UsageError("you must specify a URL");
            url = args[0];
        } else {
            Path path = resolveExprPath(lookupFileArg(*state, args.empty() ? "." : args[0]));
            Value vRoot;
            state->evalFile(path, vRoot);
            Value & v(*findAlongAttrPath(*state, attrPath, autoArgs, vRoot).first);
            state->forceAttrs(v);

            /* Extract the URL. */
            auto attr = v.attrs->find(state->symbols.create("urls"));
            if (attr == v.attrs->end())
                throw Error("attribute set does not contain a 'urls' attribute");
            state->forceList(*attr->value);
            if (attr->value->listSize() < 1)
                throw Error("'urls' list is empty");
            url = state->forceString(*attr->value->listElems()[0]);

            /* Extract the hash mode. */
            attr = v.attrs->find(state->symbols.create("outputHashMode"));
            if (attr == v.attrs->end())
                printInfo("warning: this does not look like a fetchurl call");
            else
                unpack = state->forceString(*attr->value) == "recursive";

            /* Extract the name. */
            if (!name) {
                attr = v.attrs->find(state->symbols.create("name"));
                if (attr != v.attrs->end())
                    name = state->forceString(*attr->value);
            }
        }

        std::optional<Hash> expectedHash;
        if (args.size() == 2)
            expectedHash = Hash::parseAny(args[1], ht);

        auto [storePath, hash] = prefetchFile(
            store, resolveMirrorUrl(*state, url), name, ht, expectedHash, unpack, executable);

        stopProgressBar();

        if (!printPath)
            printInfo("path is '%s'", store->printStorePath(storePath));

        std::cout << printHash16or32(hash) << std::endl;
        if (printPath)
            std::cout << store->printStorePath(storePath) << std::endl;

        return 0;
    }
}

static RegisterLegacyCommand r_nix_prefetch_url("nix-prefetch-url", main_nix_prefetch_url);

struct CmdStorePrefetchFile : StoreCommand, MixJSON
{
    std::string url;
    bool executable = false;
    std::optional<std::string> name;
    HashType hashType = htSHA256;
    std::optional<Hash> expectedHash;

    CmdStorePrefetchFile()
    {
        addFlag({
            .longName = "name",
            .description = "Override the name component of the resulting store path. It defaults to the base name of *url*.",
            .labels = {"name"},
            .handler = {&name}
        });

        addFlag({
            .longName = "expected-hash",
            .description = "The expected hash of the file.",
            .labels = {"hash"},
            .handler = {[&](std::string s) {
                expectedHash = Hash::parseAny(s, hashType);
            }}
        });

        addFlag(Flag::mkHashTypeFlag("hash-type", &hashType));

        addFlag({
            .longName = "executable",
            .description =
                "Make the resulting file executable. Note that this causes the "
                "resulting hash to be a NAR hash rather than a flat file hash.",
            .handler = {&executable, true},
        });

        expectArg("url", &url);
    }

    std::string description() override
    {
        return "download a file into the Nix store";
    }

    std::string doc() override
    {
        return
          #include "store-prefetch-file.md"
          ;
    }
    void run(ref<Store> store) override
    {
        auto [storePath, hash] = prefetchFile(store, url, name, hashType, expectedHash, false, executable);

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(SRI, true);
            logger->cout(res.dump());
        } else {
            notice("Downloaded '%s' to '%s' (hash '%s').",
                url,
                store->printStorePath(storePath),
                hash.to_string(SRI, true));
        }
    }
};

static auto rCmdStorePrefetchFile = registerCommand2<CmdStorePrefetchFile>({"store", "prefetch-file"});

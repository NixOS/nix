#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-open.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/finally.hh"
#include "nix/main/loggers.hh"
#include "nix/util/tarfile.hh"
#include "nix/expr/attr-path.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/cmd/legacy.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/cmd/misc-store-flags.hh"
#include "nix/util/terminal.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/url.hh"
#include "nix/store/path.hh"

#include "man-pages.hh"

#include <nlohmann/json.hpp>

using namespace nix;

/* If ‘url’ starts with ‘mirror://’, then resolve it using the list of
   mirrors defined in Nixpkgs. */
std::string resolveMirrorUrl(EvalState & state, const std::string & url)
{
    if (url.substr(0, 9) != "mirror://")
        return url;

    std::string s(url, 9);
    auto p = s.find('/');
    if (p == std::string::npos)
        throw Error("invalid mirror URL '%s'", url);
    std::string mirrorName(s, 0, p);

    Value vMirrors;
    // FIXME: use nixpkgs flake
    state.eval(
        state.parseExprFromString(
            "import <nixpkgs/pkgs/build-support/fetchurl/mirrors.nix>", state.rootPath(CanonPath::root)),
        vMirrors);
    state.forceAttrs(vMirrors, noPos, "while evaluating the set of all mirrors");

    auto mirrorList = vMirrors.attrs()->get(state.symbols.create(mirrorName));
    if (!mirrorList)
        throw Error("unknown mirror name '%s'", mirrorName);
    state.forceList(*mirrorList->value, noPos, "while evaluating one mirror configuration");

    if (mirrorList->value->listSize() < 1)
        throw Error("mirror URL '%s' did not expand to anything", url);

    std::string mirror(
        state.forceString(*mirrorList->value->listView()[0], noPos, "while evaluating the first available mirror"));
    return mirror + (hasSuffix(mirror, "/") ? "" : "/") + s.substr(p + 1);
}

std::tuple<StorePath, Hash> prefetchFile(
    ref<Store> store,
    const VerbatimURL & url,
    std::optional<std::string> name,
    HashAlgorithm hashAlgo,
    std::optional<Hash> expectedHash,
    bool unpack,
    bool executable)
{
    ContentAddressMethod method =
        unpack || executable ? ContentAddressMethod::Raw::NixArchive : ContentAddressMethod::Raw::Flat;

    /* Figure out a name in the Nix store. */
    if (!name) {
        name = url.lastPathSegment();
        if (!name || name->empty())
            throw Error("cannot figure out file name for '%s'", url.to_string());
    }
    try {
        checkName(*name);
    } catch (BadStorePathName & e) {
        e.addTrace({}, "file name '%s' was extracted from URL '%s'", *name, url.to_string());
        throw;
    }

    std::optional<StorePath> storePath;
    std::optional<Hash> hash;

    /* If an expected hash is given, the file may already exist in
       the store. */
    if (expectedHash) {
        hashAlgo = expectedHash->algo;
        storePath =
            store->makeFixedOutputPathFromCA(*name, ContentAddressWithReferences::fromParts(method, *expectedHash, {}));
        if (store->isValidPath(*storePath))
            hash = expectedHash;
        else
            storePath.reset();
    }

    if (!storePath) {

        AutoDelete tmpDir(createTempDir(), true);
        std::filesystem::path tmpFile = tmpDir.path() / "tmp";

        /* Download the file. */
        {
            auto mode = 0600;
            if (executable)
                mode = 0700;

            AutoCloseFD fd = toDescriptor(open(tmpFile.string().c_str(), O_WRONLY | O_CREAT | O_EXCL, mode));
            if (!fd)
                throw SysError("creating temporary file '%s'", tmpFile);

            FdSink sink(fd.get());

            FileTransferRequest req(url);
            req.decompress = false;
            getFileTransfer()->download(std::move(req), sink);
        }

        /* Optionally unpack the file. */
        if (unpack) {
            Activity act(*logger, lvlChatty, actUnknown, fmt("unpacking '%s'", url.to_string()));
            auto unpacked = (tmpDir.path() / "unpacked").string();
            createDirs(unpacked);
            unpackTarfile(tmpFile.string(), unpacked);

            auto entries = DirectoryIterator{unpacked};
            /* If the archive unpacks to a single file/directory, then use
               that as the top-level. */
            tmpFile = entries->path();
            auto fileCount = std::distance(entries, DirectoryIterator{});
            if (fileCount != 1) {
                /* otherwise, use the directory itself */
                tmpFile = unpacked;
            }
        }

        Activity act(*logger, lvlChatty, actUnknown, fmt("adding '%s' to the store", url.to_string()));

        auto info = store->addToStoreSlow(
            *name, PosixSourceAccessor::createAtRoot(tmpFile), method, hashAlgo, {}, expectedHash);
        storePath = info.path;
        assert(info.ca);
        hash = info.ca->hash;
    }

    return {storePath.value(), hash.value()};
}

static int main_nix_prefetch_url(int argc, char ** argv)
{
    {
        HashAlgorithm ha = HashAlgorithm::SHA256;
        std::vector<std::string> args;
        bool printPath = getEnv("PRINT_PATH") == "1";
        bool fromExpr = false;
        std::string attrPath;
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
                auto s = getArg(*arg, arg, end);
                ha = parseHashAlgo(s);
            } else if (*arg == "--print-path")
                printPath = true;
            else if (*arg == "--attr" || *arg == "-A") {
                fromExpr = true;
                attrPath = getArg(*arg, arg, end);
            } else if (*arg == "--unpack")
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

        setLogFormat("bar");

        auto store = openStore();
        auto state = std::make_unique<EvalState>(myArgs.lookupPath, store, fetchSettings, evalSettings);

        Bindings & autoArgs = *myArgs.getAutoArgs(*state);

        /* If -A is given, get the URL from the specified Nix
           expression. */
        std::string url;
        if (!fromExpr) {
            if (args.empty())
                throw UsageError("you must specify a URL");
            url = args[0];
        } else {
            Value vRoot;
            state->evalFile(resolveExprPath(lookupFileArg(*state, args.empty() ? "." : args[0])), vRoot);
            Value & v(*findAlongAttrPath(*state, attrPath, autoArgs, vRoot).first);
            state->forceAttrs(v, noPos, "while evaluating the source attribute to prefetch");

            /* Extract the URL. */
            auto * attr = v.attrs()->get(state->symbols.create("urls"));
            if (!attr)
                throw Error("attribute 'urls' missing");
            state->forceList(*attr->value, noPos, "while evaluating the urls to prefetch");
            if (attr->value->listSize() < 1)
                throw Error("'urls' list is empty");
            url = state->forceString(
                *attr->value->listView()[0], noPos, "while evaluating the first url from the urls list");

            /* Extract the hash mode. */
            auto attr2 = v.attrs()->get(state->symbols.create("outputHashMode"));
            if (!attr2)
                printInfo("warning: this does not look like a fetchurl call");
            else
                unpack = state->forceString(
                             *attr2->value, noPos, "while evaluating the outputHashMode of the source to prefetch")
                         == "recursive";

            /* Extract the name. */
            if (!name) {
                auto attr3 = v.attrs()->get(state->symbols.create("name"));
                if (!attr3)
                    name =
                        state->forceString(*attr3->value, noPos, "while evaluating the name of the source to prefetch");
            }
        }

        std::optional<Hash> expectedHash;
        if (args.size() == 2)
            expectedHash = Hash::parseAny(args[1], ha);

        auto [storePath, hash] =
            prefetchFile(store, resolveMirrorUrl(*state, url), name, ha, expectedHash, unpack, executable);

        logger->stop();

        if (!printPath)
            printInfo("path is '%s'", store->printStorePath(storePath));

        assert(static_cast<char>(hash.algo));
        logger->cout(hash.to_string(hash.algo == HashAlgorithm::MD5 ? HashFormat::Base16 : HashFormat::Nix32, false));

        if (printPath)
            logger->cout(store->printStorePath(storePath));

        return 0;
    }
}

static RegisterLegacyCommand r_nix_prefetch_url("nix-prefetch-url", main_nix_prefetch_url);

struct CmdStorePrefetchFile : StoreCommand, MixJSON
{
    std::string url;
    bool executable = false;
    bool unpack = false;
    std::optional<std::string> name;
    HashAlgorithm hashAlgo = HashAlgorithm::SHA256;
    std::optional<Hash> expectedHash;

    CmdStorePrefetchFile()
    {
        addFlag({
            .longName = "name",
            .description =
                "Override the name component of the resulting store path. It defaults to the base name of *url*.",
            .labels = {"name"},
            .handler = {&name},
        });

        addFlag({
            .longName = "expected-hash",
            .description = "The expected hash of the file.",
            .labels = {"hash"},
            .handler = {[&](std::string s) { expectedHash = Hash::parseAny(s, hashAlgo); }},
        });

        addFlag(flag::hashAlgo("hash-type", &hashAlgo));

        addFlag({
            .longName = "executable",
            .description = "Make the resulting file executable. Note that this causes the "
                           "resulting hash to be a NAR hash rather than a flat file hash.",
            .handler = {&executable, true},
        });

        addFlag({
            .longName = "unpack",
            .description = "Unpack the archive (which must be a tarball or zip file) and add "
                           "the result to the Nix store.",
            .handler = {&unpack, true},
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
        auto [storePath, hash] = prefetchFile(store, url, name, hashAlgo, expectedHash, unpack, executable);

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(HashFormat::SRI, true);
            printJSON(res);
        } else {
            notice(
                "Downloaded '%s' to '%s' (hash '%s').",
                url,
                store->printStorePath(storePath),
                hash.to_string(HashFormat::SRI, true));
        }
    }
};

static auto rCmdStorePrefetchFile = registerCommand2<CmdStorePrefetchFile>({"store", "prefetch-file"});

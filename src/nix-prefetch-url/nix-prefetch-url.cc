#include "hash.hh"
#include "shared.hh"
#include "filetransfer.hh"
#include "store-api.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "common-eval-args.hh"
#include "attr-path.hh"
#include "finally.hh"
#include "../nix/legacy.hh"
#include "progress-bar.hh"
#include "tarfile.hh"

#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace nix;


/* If ‘uri’ starts with ‘mirror://’, then resolve it using the list of
   mirrors defined in Nixpkgs. */
string resolveMirrorUri(EvalState & state, string uri)
{
    if (string(uri, 0, 9) != "mirror://") return uri;

    string s(uri, 9);
    auto p = s.find('/');
    if (p == string::npos) throw Error("invalid mirror URI");
    string mirrorName(s, 0, p);

    Value vMirrors;
    state.eval(state.parseExprFromString("import <nixpkgs/pkgs/build-support/fetchurl/mirrors.nix>", "."), vMirrors);
    state.forceAttrs(vMirrors);

    auto mirrorList = vMirrors.attrs->find(state.symbols.create(mirrorName));
    if (mirrorList == vMirrors.attrs->end())
        throw Error("unknown mirror name '%1%'", mirrorName);
    state.forceList(*mirrorList->value);

    if (mirrorList->value->listSize() < 1)
        throw Error("mirror URI '%1%' did not expand to anything", uri);

    string mirror = state.forceString(*mirrorList->value->listElems()[0]);
    return mirror + (hasSuffix(mirror, "/") ? "" : "/") + string(s, p + 1);
}


static int _main(int argc, char * * argv)
{
    {
        HashType ht = htSHA256;
        std::vector<string> args;
        bool printPath = getEnv("PRINT_PATH") == "1";
        bool fromExpr = false;
        string attrPath;
        bool unpack = false;
        bool executable = false;
        string name;

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

        initPlugins();

        if (args.size() > 2)
            throw UsageError("too many arguments");

        Finally f([]() { stopProgressBar(); });

        if (isatty(STDERR_FILENO))
          startProgressBar();

        auto store = openStore();
        auto state = std::make_unique<EvalState>(myArgs.searchPath, store);

        Bindings & autoArgs = *myArgs.getAutoArgs(*state);

        /* If -A is given, get the URI from the specified Nix
           expression. */
        string uri;
        if (!fromExpr) {
            if (args.empty())
                throw UsageError("you must specify a URI");
            uri = args[0];
        } else {
            Path path = resolveExprPath(lookupFileArg(*state, args.empty() ? "." : args[0]));
            Value vRoot;
            state->evalFile(path, vRoot);
            Value & v(*findAlongAttrPath(*state, attrPath, autoArgs, vRoot).first);
            state->forceAttrs(v);

            /* Extract the URI. */
            auto attr = v.attrs->find(state->symbols.create("urls"));
            if (attr == v.attrs->end())
                throw Error("attribute set does not contain a 'urls' attribute");
            state->forceList(*attr->value);
            if (attr->value->listSize() < 1)
                throw Error("'urls' list is empty");
            uri = state->forceString(*attr->value->listElems()[0]);

            /* Extract the hash mode. */
            attr = v.attrs->find(state->symbols.create("outputHashMode"));
            if (attr == v.attrs->end())
                printInfo("warning: this does not look like a fetchurl call");
            else
                unpack = state->forceString(*attr->value) == "recursive";

            /* Extract the name. */
            if (name.empty()) {
                attr = v.attrs->find(state->symbols.create("name"));
                if (attr != v.attrs->end())
                    name = state->forceString(*attr->value);
            }
        }

        /* Figure out a name in the Nix store. */
        if (name.empty())
            name = baseNameOf(uri);
        if (name.empty())
            throw Error("cannot figure out file name for '%1%'", uri);

        /* If an expected hash is given, the file may already exist in
           the store. */
        std::optional<Hash> expectedHash;
        Hash hash(ht);
        std::optional<StorePath> storePath;
        if (args.size() == 2) {
            expectedHash = Hash::parseAny(args[1], ht);
            const auto method = unpack ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat;
            storePath = store->makeFixedOutputPath(name, FixedOutputInfo {
                {
                    .method = method,
                    .hash = *expectedHash,
                },
                {},
            });
            if (store->isValidPath(*storePath))
                hash = *expectedHash;
            else
                storePath.reset();
        }

        if (!storePath) {

            auto actualUri = resolveMirrorUri(*state, uri);

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

                FileTransferRequest req(actualUri);
                req.decompress = false;
                getFileTransfer()->download(std::move(req), sink);
            }

            /* Optionally unpack the file. */
            if (unpack) {
                printInfo("unpacking...");
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

            const auto method = unpack || executable ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat;

            auto info = store->addToStoreSlow(name, tmpFile, method, ht, expectedHash);
            storePath = info.path;
            assert(info.ca);
            hash = getContentAddressHash(*info.ca);
        }

        stopProgressBar();

        if (!printPath)
            printInfo("path is '%s'", store->printStorePath(*storePath));

        std::cout << printHash16or32(hash) << std::endl;
        if (printPath)
            std::cout << store->printStorePath(*storePath) << std::endl;

        return 0;
    }
}

static RegisterLegacyCommand s1("nix-prefetch-url", _main);

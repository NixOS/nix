#include "hash.hh"
#include "shared.hh"
#include "download.hh"
#include "store-api.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "common-opts.hh"
#include "attr-path.hh"

#include <iostream>

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
        throw Error(format("unknown mirror name ‘%1%’") % mirrorName);
    state.forceList(*mirrorList->value);

    if (mirrorList->value->listSize() < 1)
        throw Error(format("mirror URI ‘%1%’ did not expand to anything") % uri);

    string mirror = state.forceString(*mirrorList->value->listElems()[0]);
    return mirror + (hasSuffix(mirror, "/") ? "" : "/") + string(s, p + 1);
}


int main(int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();
        initGC();

        HashType ht = htSHA256;
        std::vector<string> args;
        Strings searchPath;
        bool printPath = getEnv("PRINT_PATH") != "";
        bool fromExpr = false;
        string attrPath;
        std::map<string, string> autoArgs_;
        bool unpack = false;
        string name;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-prefetch-url");
            else if (*arg == "--version")
                printVersion("nix-prefetch-url");
            else if (*arg == "--type") {
                string s = getArg(*arg, arg, end);
                ht = parseHashType(s);
                if (ht == htUnknown)
                    throw UsageError(format("unknown hash type ‘%1%’") % s);
            }
            else if (*arg == "--print-path")
                printPath = true;
            else if (*arg == "--attr" || *arg == "-A") {
                fromExpr = true;
                attrPath = getArg(*arg, arg, end);
            }
            else if (*arg == "--unpack")
                unpack = true;
            else if (*arg == "--name")
                name = getArg(*arg, arg, end);
            else if (parseAutoArgs(arg, end, autoArgs_))
                ;
            else if (parseSearchPathArg(arg, end, searchPath))
                ;
            else if (*arg != "" && arg->at(0) == '-')
                return false;
            else
                args.push_back(*arg);
            return true;
        });

        if (args.size() > 2)
            throw UsageError("too many arguments");

        auto store = openStore();
        EvalState state(searchPath, store);

        Bindings & autoArgs(*evalAutoArgs(state, autoArgs_));

        /* If -A is given, get the URI from the specified Nix
           expression. */
        string uri;
        if (!fromExpr) {
            if (args.empty())
                throw UsageError("you must specify a URI");
            uri = args[0];
        } else {
            Path path = resolveExprPath(lookupFileArg(state, args.empty() ? "." : args[0]));
            Value vRoot;
            state.evalFile(path, vRoot);
            Value & v(*findAlongAttrPath(state, attrPath, autoArgs, vRoot));
            state.forceAttrs(v);

            /* Extract the URI. */
            auto attr = v.attrs->find(state.symbols.create("urls"));
            if (attr == v.attrs->end())
                throw Error("attribute set does not contain a ‘urls’ attribute");
            state.forceList(*attr->value);
            if (attr->value->listSize() < 1)
                throw Error("‘urls’ list is empty");
            uri = state.forceString(*attr->value->listElems()[0]);

            /* Extract the hash mode. */
            attr = v.attrs->find(state.symbols.create("outputHashMode"));
            if (attr == v.attrs->end())
                printInfo("warning: this does not look like a fetchurl call");
            else
                unpack = state.forceString(*attr->value) == "recursive";

            /* Extract the name. */
            if (name.empty()) {
                attr = v.attrs->find(state.symbols.create("name"));
                if (attr != v.attrs->end())
                    name = state.forceString(*attr->value);
            }
        }

        /* Figure out a name in the Nix store. */
        if (name.empty())
            name = baseNameOf(uri);
        if (name.empty())
            throw Error(format("cannot figure out file name for ‘%1%’") % uri);

        /* If an expected hash is given, the file may already exist in
           the store. */
        Hash hash, expectedHash(ht);
        Path storePath;
        if (args.size() == 2) {
            expectedHash = parseHash16or32(ht, args[1]);
            storePath = store->makeFixedOutputPath(unpack, expectedHash, name);
            if (store->isValidPath(storePath))
                hash = expectedHash;
            else
                storePath.clear();
        }

        if (storePath.empty()) {

            auto actualUri = resolveMirrorUri(state, uri);

            /* Download the file. */
            auto result = getDownloader()->download(DownloadRequest(actualUri));

            AutoDelete tmpDir(createTempDir(), true);
            Path tmpFile = (Path) tmpDir + "/tmp";
            writeFile(tmpFile, *result.data);

            /* Optionally unpack the file. */
            if (unpack) {
                printInfo("unpacking...");
                Path unpacked = (Path) tmpDir + "/unpacked";
                createDirs(unpacked);
                if (hasSuffix(baseNameOf(uri), ".zip"))
                    runProgram("unzip", true, {"-qq", tmpFile, "-d", unpacked}, "");
                else
                    // FIXME: this requires GNU tar for decompression.
                    runProgram("tar", true, {"xf", tmpFile, "-C", unpacked}, "");

                /* If the archive unpacks to a single file/directory, then use
                   that as the top-level. */
                auto entries = readDirectory(unpacked);
                if (entries.size() == 1)
                    tmpFile = unpacked + "/" + entries[0].name;
                else
                    tmpFile = unpacked;
            }

            /* FIXME: inefficient; addToStore() will also hash
               this. */
            hash = unpack ? hashPath(ht, tmpFile).first : hashString(ht, *result.data);

            if (expectedHash != Hash(ht) && expectedHash != hash)
                throw Error(format("hash mismatch for ‘%1%’") % uri);

            /* Copy the file to the Nix store. FIXME: if RemoteStore
               implemented addToStoreFromDump() and downloadFile()
               supported a sink, we could stream the download directly
               into the Nix store. */
            storePath = store->addToStore(name, tmpFile, unpack, ht);

            assert(storePath == store->makeFixedOutputPath(unpack, hash, name));
        }

        if (!printPath)
            printInfo(format("path is ‘%1%’") % storePath);

        std::cout << printHash16or32(hash) << std::endl;
        if (printPath)
            std::cout << storePath << std::endl;
    });
}

#include "command.hh"
#include "hash.hh"
#include "legacy.hh"
#include "shared.hh"
#include "references.hh"
#include "archive.hh"

using namespace nix;

struct CmdHash : Command
{
    FileIngestionMethod mode;
    bool truncate = false;
    HashType ht = htSHA256;
    HashEncoding encoding = SRI;
    std::vector<std::string> paths;
    std::optional<std::string> modulus;

    CmdHash(FileIngestionMethod mode) : mode(mode)
    {
        mkFlag(0, "base16", "print hash in base16", &encoding, Base16);
        mkFlag(0, "base32", "print hash in base32 (Nix-specific)", &encoding, Base32);
        mkFlag(0, "base64", "print hash in base64", &encoding, Base64);
        mkFlag(0, "sri", "print hash in SRI format", &encoding, SRI);
        addFlag(Flag::mkHashTypeFlag("type", &ht));
        #if 0
        mkFlag()
            .longName("modulo")
            .description("compute hash modulo specified string")
            .labels({"modulus"})
            .dest(&modulus);
        #endif
        expectArgs("paths", &paths);
    }

    std::string description() override
    {
        const char* d;
        switch (mode) {
        case FileIngestionMethod::Flat:
            d = "print cryptographic hash of a regular file";
        case FileIngestionMethod::Recursive:
            d = "print cryptographic hash of the NAR serialisation of a path";
        };
        return d;
    }

    Category category() override { return catUtility; }

    void run() override
    {
        for (auto path : paths) {

            std::unique_ptr<AbstractHashSink> hashSink;
            if (modulus)
                hashSink = std::make_unique<HashModuloSink>(ht, *modulus);
            else
                hashSink = std::make_unique<HashSink>(ht);

            switch (mode) {
            case FileIngestionMethod::Flat:
                readFile(path, *hashSink);
                break;
            case FileIngestionMethod::Recursive:
                dumpPath(path, *hashSink);
                break;
            }

            Hash h = hashSink->finish().first;
            if (truncate && h.hashSize > 20) h = compressHash(h, 20);
            logger->stdout(h.to_string(encoding));
        }
    }
};

static RegisterCommand r1("hash-file", [](){ return make_ref<CmdHash>(FileIngestionMethod::Flat); });
static RegisterCommand r2("hash-path", [](){ return make_ref<CmdHash>(FileIngestionMethod::Recursive); });

struct CmdToBase : Command
{
    HashEncoding encoding;
    HashType ht = htUnknown;
    std::vector<std::string> args;

    CmdToBase(HashEncoding encoding) : encoding(encoding)
    {
        addFlag(Flag::mkHashTypeFlag("type", &ht));
        expectArgs("strings", &args);
    }

    std::string description() override
    {
        return fmt("convert a hash to %s representation",
            encoding == Base16 ? "base16" :
            encoding == Base32 ? "base32" :
            encoding == Base64 ? "base64" :
            encoding == PrefixedBase16 ? "prefixed-base16" :
            encoding == PrefixedBase32 ? "prefixed-base32" :
            encoding == PrefixedBase64 ? "prefixed-base64" :
            "SRI");
    }

    Category category() override { return catUtility; }

    void run() override
    {
        for (auto s : args)
            logger->stdout(Hash(s, ht).to_string(encoding));
    }
};

static RegisterCommand r3("to-base16", [](){ return make_ref<CmdToBase>(Base16); });
static RegisterCommand r4("to-base32", [](){ return make_ref<CmdToBase>(Base32); });
static RegisterCommand r5("to-base64", [](){ return make_ref<CmdToBase>(Base64); });
static RegisterCommand r6("to-sri", [](){ return make_ref<CmdToBase>(SRI); });

/* Legacy nix-hash command. */
static int compatNixHash(int argc, char * * argv)
{
    HashType ht = htMD5;
    bool flat = false;
    bool base32 = false;
    bool truncate = false;
    enum { opHash, opTo32, opTo16 } op = opHash;
    std::vector<std::string> ss;

    parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
        if (*arg == "--help")
            showManPage("nix-hash");
        else if (*arg == "--version")
            printVersion("nix-hash");
        else if (*arg == "--flat") flat = true;
        else if (*arg == "--base32") base32 = true;
        else if (*arg == "--truncate") truncate = true;
        else if (*arg == "--type") {
            string s = getArg(*arg, arg, end);
            ht = parseHashType(s);
            if (ht == htUnknown)
                throw UsageError("unknown hash type '%1%'", s);
        }
        else if (*arg == "--to-base16") op = opTo16;
        else if (*arg == "--to-base32") op = opTo32;
        else if (*arg != "" && arg->at(0) == '-')
            return false;
        else
            ss.push_back(*arg);
        return true;
    });

    if (op == opHash) {
        CmdHash cmd(flat ? FileIngestionMethod::Flat : FileIngestionMethod::Recursive);
        cmd.ht = ht;
        cmd.encoding = base32 ? Base32 : Base16;
        cmd.truncate = truncate;
        cmd.paths = ss;
        cmd.run();
    }

    else {
        CmdToBase cmd(op == opTo32 ? Base32 : Base16);
        cmd.args = ss;
        cmd.ht = ht;
        cmd.run();
    }

    return 0;
}

static RegisterLegacyCommand s1("nix-hash", compatNixHash);

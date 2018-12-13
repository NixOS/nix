#include "command.hh"
#include "hash.hh"
#include "legacy.hh"
#include "shared.hh"

using namespace nix;

struct CmdHash : Command
{
    enum Mode { mFile, mPath };
    Mode mode;
    Base base = SRI;
    bool truncate = false;
    HashType ht = htSHA256;
    std::vector<std::string> paths;

    CmdHash(Mode mode) : mode(mode)
    {
        mkFlag(0, "sri", "print hash in SRI format", &base, SRI);
        mkFlag(0, "base64", "print hash in base-64", &base, Base64);
        mkFlag(0, "base32", "print hash in base-32 (Nix-specific)", &base, Base32);
        mkFlag(0, "base16", "print hash in base-16", &base, Base16);
        mkFlag()
            .longName("type")
            .mkHashTypeFlag(&ht);
        expectArgs("paths", &paths);
    }

    std::string name() override
    {
        return mode == mFile ? "hash-file" : "hash-path";
    }

    std::string description() override
    {
        return mode == mFile
            ? "print cryptographic hash of a regular file"
            : "print cryptographic hash of the NAR serialisation of a path";
    }

    void run() override
    {
        for (auto path : paths) {
            Hash h = mode == mFile ? hashFile(ht, path) : hashPath(ht, path).first;
            if (truncate && h.hashSize > 20) h = compressHash(h, 20);
            std::cout << format("%1%\n") %
                h.to_string(base, base == SRI);
        }
    }
};

static RegisterCommand r1(make_ref<CmdHash>(CmdHash::mFile));
static RegisterCommand r2(make_ref<CmdHash>(CmdHash::mPath));

struct CmdToBase : Command
{
    Base base;
    HashType ht = htUnknown;
    std::vector<std::string> args;

    CmdToBase(Base base) : base(base)
    {
        mkFlag()
            .longName("type")
            .mkHashTypeFlag(&ht);
        expectArgs("strings", &args);
    }

    std::string name() override
    {
        return
            base == Base16 ? "to-base16" :
            base == Base32 ? "to-base32" :
            base == Base64 ? "to-base64" :
            "to-sri";
    }

    std::string description() override
    {
        return fmt("convert a hash to %s representation",
            base == Base16 ? "base-16" :
            base == Base32 ? "base-32" :
            base == Base64 ? "base-64" :
            "SRI");
    }

    void run() override
    {
        for (auto s : args)
            std::cout << fmt("%s\n", Hash(s, ht).to_string(base, base == SRI));
    }
};

static RegisterCommand r3(make_ref<CmdToBase>(Base16));
static RegisterCommand r4(make_ref<CmdToBase>(Base32));
static RegisterCommand r5(make_ref<CmdToBase>(Base64));
static RegisterCommand r6(make_ref<CmdToBase>(SRI));

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
                throw UsageError(format("unknown hash type '%1%'") % s);
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
        CmdHash cmd(flat ? CmdHash::mFile : CmdHash::mPath);
        cmd.ht = ht;
        cmd.base = base32 ? Base32 : Base16;
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

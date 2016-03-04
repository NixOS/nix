#include "command.hh"
#include "hash.hh"
#include "legacy.hh"
#include "shared.hh"

using namespace nix;

struct CmdHash : Command
{
    enum Mode { mFile, mPath };
    Mode mode;
    bool base32 = false;
    bool truncate = false;
    HashType ht = htSHA512;
    Strings paths;

    CmdHash(Mode mode) : mode(mode)
    {
        mkFlag(0, "base32", "print hash in base-32", &base32);
        mkFlag(0, "base16", "print hash in base-16", &base32, false);
        mkHashTypeFlag("type", &ht);
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
                (base32 ? printHash32(h) : printHash(h));
        }
    }
};

static RegisterCommand r1(make_ref<CmdHash>(CmdHash::mFile));
static RegisterCommand r2(make_ref<CmdHash>(CmdHash::mPath));

struct CmdToBase : Command
{
    bool toBase32;
    HashType ht = htSHA512;
    Strings args;

    CmdToBase(bool toBase32) : toBase32(toBase32)
    {
        mkHashTypeFlag("type", &ht);
        expectArgs("strings", &args);
    }

    std::string name() override
    {
        return toBase32 ? "to-base32" : "to-base16";
    }

    std::string description() override
    {
        return toBase32
            ? "convert a hash to base-32 representation"
            : "convert a hash to base-16 representation";
    }

    void run() override
    {
        for (auto s : args) {
            Hash h = parseHash16or32(ht, s);
            std::cout << format("%1%\n") %
                (toBase32 ? printHash32(h) : printHash(h));
        }
    }
};

static RegisterCommand r3(make_ref<CmdToBase>(false));
static RegisterCommand r4(make_ref<CmdToBase>(true));

/* Legacy nix-hash command. */
static int compatNixHash(int argc, char * * argv)
{
    HashType ht = htMD5;
    bool flat = false;
    bool base32 = false;
    bool truncate = false;
    enum { opHash, opTo32, opTo16 } op = opHash;
    Strings ss;

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
                throw UsageError(format("unknown hash type ‘%1%’") % s);
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
        cmd.base32 = base32;
        cmd.truncate = truncate;
        cmd.paths = ss;
        cmd.run();
    }

    else {
        CmdToBase cmd(op == opTo32);
        cmd.args = ss;
        cmd.ht = ht;
        cmd.run();
    }

    return 0;
}

static RegisterLegacyCommand s1("nix-hash", compatNixHash);

#include "command.hh"
#include "hash.hh"
#include "content-address.hh"
#include "legacy.hh"
#include "shared.hh"
#include "references.hh"
#include "archive.hh"

using namespace nix;

struct CmdHashBase : Command
{
    FileIngestionMethod mode;
    HashFormat hashFormat = HashFormat::SRI;
    bool truncate = false;
    HashType ht = htSHA256;
    std::vector<std::string> paths;
    std::optional<std::string> modulus;

    CmdHashBase(FileIngestionMethod mode) : mode(mode)
    {
        addFlag({
            .longName = "sri",
            .description = "Print the hash in SRI format.",
            .handler = {&hashFormat, HashFormat::SRI},
        });

        addFlag({
            .longName = "base64",
            .description = "Print the hash in base-64 format.",
            .handler = {&hashFormat, HashFormat::Base64},
        });

        addFlag({
            .longName = "base32",
            .description = "Print the hash in base-32 (Nix-specific) format.",
            .handler = {&hashFormat, HashFormat::Base32},
        });

        addFlag({
            .longName = "base16",
            .description = "Print the hash in base-16 format.",
            .handler = {&hashFormat, HashFormat::Base16},
        });

        addFlag(Flag::mkHashTypeFlag("type", &ht));

        #if 0
        addFlag({
            .longName = "modulo",
            .description = "Compute the hash modulo the specified string.",
            .labels = {"modulus"},
            .handler = {&modulus},
        });
        #endif\

        expectArgs({
            .label = "paths",
            .handler = {&paths},
            .completer = completePath
        });
    }

    std::string description() override
    {
        switch (mode) {
        case FileIngestionMethod::Flat:
            return  "print cryptographic hash of a regular file";
        case FileIngestionMethod::Recursive:
            return "print cryptographic hash of the NAR serialisation of a path";
        default:
            assert(false);
        };
    }

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
            logger->cout(h.to_string(hashFormat, hashFormat == HashFormat::SRI));
        }
    }
};

struct CmdToBase : Command
{
    HashFormat hashFormat;
    std::optional<HashType> ht;
    std::vector<std::string> args;

    CmdToBase(HashFormat hashFormat) : hashFormat(hashFormat)
    {
        addFlag(Flag::mkHashTypeOptFlag("type", &ht));
        expectArgs("strings", &args);
    }

    std::string description() override
    {
        return fmt("convert a hash to %s representation",
            hashFormat == HashFormat::Base16 ? "base-16" :
            hashFormat == HashFormat::Base32 ? "base-32" :
            hashFormat == HashFormat::Base64 ? "base-64" :
            "SRI");
    }

    void run() override
    {
        for (auto s : args)
            logger->cout(Hash::parseAny(s, ht).to_string(hashFormat, hashFormat == HashFormat::SRI));
    }
};

struct CmdHash : NixMultiCommand
{
    CmdHash()
        : MultiCommand({
                {"file", []() { return make_ref<CmdHashBase>(FileIngestionMethod::Flat);; }},
                {"path", []() { return make_ref<CmdHashBase>(FileIngestionMethod::Recursive); }},
                {"to-base16", []() { return make_ref<CmdToBase>(HashFormat::Base16); }},
                {"to-base32", []() { return make_ref<CmdToBase>(HashFormat::Base32); }},
                {"to-base64", []() { return make_ref<CmdToBase>(HashFormat::Base64); }},
                {"to-sri", []() { return make_ref<CmdToBase>(HashFormat::SRI); }},
          })
    { }

    std::string description() override
    {
        return "compute and convert cryptographic hashes";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix hash' requires a sub-command.");
        command->second->run();
    }
};

static auto rCmdHash = registerCommand<CmdHash>("hash");

/* Legacy nix-hash command. */
static int compatNixHash(int argc, char * * argv)
{
    std::optional<HashType> ht;
    bool flat = false;
    HashFormat hashFormat = HashFormat::Base16;
    bool truncate = false;
    enum { opHash, opTo } op = opHash;
    std::vector<std::string> ss;

    parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
        if (*arg == "--help")
            showManPage("nix-hash");
        else if (*arg == "--version")
            printVersion("nix-hash");
        else if (*arg == "--flat") flat = true;
        else if (*arg == "--base16") hashFormat = HashFormat::Base16;
        else if (*arg == "--base32") hashFormat = HashFormat::Base32;
        else if (*arg == "--base64") hashFormat = HashFormat::Base64;
        else if (*arg == "--sri") hashFormat = HashFormat::SRI;
        else if (*arg == "--truncate") truncate = true;
        else if (*arg == "--type") {
            std::string s = getArg(*arg, arg, end);
            ht = parseHashType(s);
        }
        else if (*arg == "--to-base16") {
            op = opTo;
            hashFormat = HashFormat::Base16;
        }
        else if (*arg == "--to-base32") {
            op = opTo;
            hashFormat = HashFormat::Base32;
        }
        else if (*arg == "--to-base64") {
            op = opTo;
            hashFormat = HashFormat::Base64;
        }
        else if (*arg == "--to-sri") {
            op = opTo;
            hashFormat = HashFormat::SRI;
        }
        else if (*arg != "" && arg->at(0) == '-')
            return false;
        else
            ss.push_back(*arg);
        return true;
    });

    if (op == opHash) {
        CmdHashBase cmd(flat ? FileIngestionMethod::Flat : FileIngestionMethod::Recursive);
        if (!ht.has_value()) ht = htMD5;
        cmd.ht = ht.value();
        cmd.hashFormat = hashFormat;
        cmd.truncate = truncate;
        cmd.paths = ss;
        cmd.run();
    }

    else {
        CmdToBase cmd(hashFormat);
        cmd.args = ss;
        if (ht.has_value()) cmd.ht = ht;
        cmd.run();
    }

    return 0;
}

static RegisterLegacyCommand r_nix_hash("nix-hash", compatNixHash);

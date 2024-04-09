#include "command.hh"
#include "hash.hh"
#include "content-address.hh"
#include "legacy.hh"
#include "shared.hh"
#include "references.hh"
#include "archive.hh"
#include "git.hh"
#include "posix-source-accessor.hh"
#include "misc-store-flags.hh"

using namespace nix;

/**
 * Base for `nix hash path`, `nix hash file` (deprecated), and `nix-hash` (legacy).
 *
 * Deprecation Issue: https://github.com/NixOS/nix/issues/8876
 */
struct CmdHashBase : Command
{
    FileIngestionMethod mode;
    HashFormat hashFormat = HashFormat::SRI;
    bool truncate = false;
    HashAlgorithm hashAlgo = HashAlgorithm::SHA256;
    std::vector<std::string> paths;
    std::optional<std::string> modulus;

    explicit CmdHashBase(FileIngestionMethod mode) : mode(mode)
    {
        expectArgs({
            .label = "paths",
            .handler = {&paths},
            .completer = completePath
        });

        // FIXME The following flags should be deprecated, but we don't
        // yet have a mechanism for that.

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
            .handler = {&hashFormat, HashFormat::Nix32},
        });

        addFlag({
            .longName = "base16",
            .description = "Print the hash in base-16 format.",
            .handler = {&hashFormat, HashFormat::Base16},
        });

        addFlag(flag::hashAlgo("type", &hashAlgo));
    }

    std::string description() override
    {
        switch (mode) {
        case FileIngestionMethod::Flat:
            return "print cryptographic hash of a regular file";
        case FileIngestionMethod::Recursive:
            return "print cryptographic hash of the NAR serialisation of a path";
        case FileIngestionMethod::Git:
            return "print cryptographic hash of the Git serialisation of a path";
        default:
            assert(false);
        };
    }

    void run() override
    {
        for (auto path : paths) {
            auto makeSink = [&]() -> std::unique_ptr<AbstractHashSink> {
                if (modulus)
                    return std::make_unique<HashModuloSink>(hashAlgo, *modulus);
                else
                    return std::make_unique<HashSink>(hashAlgo);
            };

            auto [accessor_, canonPath] = PosixSourceAccessor::createAtRoot(path);
            auto & accessor = accessor_;
            Hash h { HashAlgorithm::SHA256 }; // throwaway def to appease C++
            switch (mode) {
            case FileIngestionMethod::Flat:
            case FileIngestionMethod::Recursive:
            {
                auto hashSink = makeSink();
                dumpPath(accessor, canonPath, *hashSink, (FileSerialisationMethod) mode);
                h = hashSink->finish().first;
                break;
            }
            case FileIngestionMethod::Git: {
                std::function<git::DumpHook> hook;
                hook = [&](const CanonPath & path) -> git::TreeEntry {
                    auto hashSink = makeSink();
                    auto mode = dump(accessor, path, *hashSink, hook);
                    auto hash = hashSink->finish().first;
                    return {
                        .mode = mode,
                        .hash = hash,
                    };
                };
                h = hook(canonPath).hash;
                break;
            }
            }

            if (truncate && h.hashSize > 20) h = compressHash(h, 20);
            logger->cout(h.to_string(hashFormat, hashFormat == HashFormat::SRI));
        }
    }
};

/**
 * `nix hash path`
 */
struct CmdHashPath : CmdHashBase
{
    CmdHashPath()
        : CmdHashBase(FileIngestionMethod::Recursive)
    {
        addFlag(flag::hashAlgo("algo", &hashAlgo));
        addFlag(flag::fileIngestionMethod(&mode));
        addFlag(flag::hashFormatWithDefault("format", &hashFormat));
        #if 0
        addFlag({
            .longName = "modulo",
            .description = "Compute the hash modulo the specified string.",
            .labels = {"modulus"},
            .handler = {&modulus},
        });
        #endif
    }
};

/**
 * For deprecated `nix hash file`
 *
 * Deprecation Issue: https://github.com/NixOS/nix/issues/8876
 */
struct CmdHashFile : CmdHashBase
{
    CmdHashFile()
        : CmdHashBase(FileIngestionMethod::Flat)
    {
    }
};

/**
 * For deprecated `nix hash to-*`
 */
struct CmdToBase : Command
{
    HashFormat hashFormat;
    std::optional<HashAlgorithm> hashAlgo;
    std::vector<std::string> args;

    CmdToBase(HashFormat hashFormat) : hashFormat(hashFormat)
    {
        addFlag(flag::hashAlgoOpt("type", &hashAlgo));
        expectArgs("strings", &args);
    }

    std::string description() override
    {
        return fmt("convert a hash to %s representation (deprecated, use `nix hash convert` instead)",
            hashFormat == HashFormat::Base16 ? "base-16" :
            hashFormat == HashFormat::Nix32 ? "base-32" :
            hashFormat == HashFormat::Base64 ? "base-64" :
            "SRI");
    }

    void run() override
    {
        warn("The old format conversion sub commands of `nix hash` where deprecated in favor of `nix hash convert`.");
        for (auto s : args)
            logger->cout(Hash::parseAny(s, hashAlgo).to_string(hashFormat, hashFormat == HashFormat::SRI));
    }
};

/**
 * `nix hash convert`
 */
struct CmdHashConvert : Command
{
    std::optional<HashFormat> from;
    HashFormat to;
    std::optional<HashAlgorithm> algo;
    std::vector<std::string> hashStrings;

    CmdHashConvert(): to(HashFormat::SRI) {
        addFlag(flag::hashFormatOpt("from", &from));
        addFlag(flag::hashFormatWithDefault("to", &to));
        addFlag(flag::hashAlgoOpt(&algo));
        expectArgs({
           .label = "hashes",
           .handler = {&hashStrings},
        });
    }

    std::string description() override
    {
        return "convert between hash formats";
    }

    std::string doc() override
    {
        return
          #include "hash-convert.md"
          ;
    }

    Category category() override { return catUtility; }

    void run() override {
        for (const auto& s: hashStrings) {
            Hash h = Hash::parseAny(s, algo);
            if (from && h.to_string(*from, from == HashFormat::SRI) != s) {
                auto from_as_string = printHashFormat(*from);
                throw BadHash("input hash '%s' does not have the expected format '--from %s'", s, from_as_string);
            }
            logger->cout(h.to_string(to, to == HashFormat::SRI));
        }
    }
};

struct CmdHash : NixMultiCommand
{
    CmdHash()
        : NixMultiCommand(
            "hash",
            {
                {"convert", []() { return make_ref<CmdHashConvert>();}},
                {"path", []() { return make_ref<CmdHashPath>(); }},
                {"file", []() { return make_ref<CmdHashFile>(); }},
                {"to-base16", []() { return make_ref<CmdToBase>(HashFormat::Base16); }},
                {"to-base32", []() { return make_ref<CmdToBase>(HashFormat::Nix32); }},
                {"to-base64", []() { return make_ref<CmdToBase>(HashFormat::Base64); }},
                {"to-sri", []() { return make_ref<CmdToBase>(HashFormat::SRI); }},
          })
    { }

    std::string description() override
    {
        return "compute and convert cryptographic hashes";
    }

    Category category() override { return catUtility; }
};

static auto rCmdHash = registerCommand<CmdHash>("hash");

/* Legacy nix-hash command. */
static int compatNixHash(int argc, char * * argv)
{
    // Wait until `nix hash convert` is not hidden behind experimental flags anymore.
    // warn("`nix-hash` has been deprecated in favor of `nix hash convert`.");

    std::optional<HashAlgorithm> hashAlgo;
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
        else if (*arg == "--base32") hashFormat = HashFormat::Nix32;
        else if (*arg == "--base64") hashFormat = HashFormat::Base64;
        else if (*arg == "--sri") hashFormat = HashFormat::SRI;
        else if (*arg == "--truncate") truncate = true;
        else if (*arg == "--type") {
            std::string s = getArg(*arg, arg, end);
            hashAlgo = parseHashAlgo(s);
        }
        else if (*arg == "--to-base16") {
            op = opTo;
            hashFormat = HashFormat::Base16;
        }
        else if (*arg == "--to-base32") {
            op = opTo;
            hashFormat = HashFormat::Nix32;
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
        if (!hashAlgo.has_value()) hashAlgo = HashAlgorithm::MD5;
        cmd.hashAlgo = hashAlgo.value();
        cmd.hashFormat = hashFormat;
        cmd.truncate = truncate;
        cmd.paths = ss;
        cmd.run();
    }

    else {
        CmdToBase cmd(hashFormat);
        cmd.args = ss;
        if (hashAlgo.has_value()) cmd.hashAlgo = hashAlgo;
        cmd.run();
    }

    return 0;
}

static RegisterLegacyCommand r_nix_hash("nix-hash", compatNixHash);

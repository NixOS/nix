#include "nix/util/args.hh"
#include "nix/store/content-address.hh"
#include "nix/main/common-args.hh"

namespace nix {

/**
 * @brief Tag type for JSON hash output format.
 *
 * JSON format outputs `{"algorithm": "<algo>", "hash": "<base16>"}`.
 */
struct OutputFormatJSON
{
    bool operator==(const OutputFormatJSON &) const = default;
    auto operator<=>(const OutputFormatJSON &) const = default;
};

/**
 * @brief Output hash format: either a HashFormat or JSON.
 */
struct OutputHashFormat
{
    using Raw = std::variant<HashFormat, OutputFormatJSON>;
    Raw raw;

    MAKE_WRAPPER_CONSTRUCTOR(OutputHashFormat);

    bool operator==(const OutputHashFormat &) const = default;
    auto operator<=>(const OutputHashFormat &) const = default;

    /// Convenience constant for JSON format
    static constexpr struct OutputFormatJSON JSON{};

    /**
     * Parse an output hash format from a string.
     *
     * Accepts all HashFormat names plus "json-base16".
     */
    static OutputHashFormat parse(std::string_view s);

    /**
     * The reverse of parse.
     */
    std::string_view print() const;

    /**
     * Parse a hash from a string representation, returning both the hash
     * and the output format it was parsed from.
     *
     * Tries to parse as JSON first (returning OutputFormatJSON if successful),
     * then falls back to Hash::parseAnyReturningFormat.
     */
    static std::pair<Hash, OutputHashFormat>
    parseAnyReturningFormat(std::string_view s, std::optional<HashAlgorithm> optAlgo);
};

/**
 * Print a hash in the specified output format.
 */
void printHash(const Hash & h, const OutputHashFormat & format, MixPrintJSON & printer);

} // namespace nix

namespace nix::flag {

Args::Flag hashAlgo(std::string && longName, HashAlgorithm * ha);

static inline Args::Flag hashAlgo(HashAlgorithm * ha)
{
    return hashAlgo("hash-algo", ha);
}

Args::Flag hashAlgoOpt(std::string && longName, std::optional<HashAlgorithm> * oha);
Args::Flag hashFormatWithDefault(std::string && longName, OutputHashFormat * hf);
Args::Flag hashFormatOpt(std::string && longName, std::optional<OutputHashFormat> * ohf);

static inline Args::Flag hashAlgoOpt(std::optional<HashAlgorithm> * oha)
{
    return hashAlgoOpt("hash-algo", oha);
}

Args::Flag fileIngestionMethod(FileIngestionMethod * method);
Args::Flag contentAddressMethod(ContentAddressMethod * method);

} // namespace nix::flag

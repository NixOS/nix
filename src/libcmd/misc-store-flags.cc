#include "nix/cmd/misc-store-flags.hh"
#include "nix/util/json-utils.hh"

namespace nix {

constexpr static std::string_view jsonFormatName = "json-base16";

OutputHashFormat OutputHashFormat::parse(std::string_view s)
{
    if (s == jsonFormatName) {
        return OutputHashFormat::JSON;
    }
    return parseHashFormat(s);
}

std::string_view OutputHashFormat::print() const
{
    return std::visit(
        overloaded{
            [](const HashFormat & hf) -> std::string_view { return printHashFormat(hf); },
            [](const OutputFormatJSON &) -> std::string_view { return jsonFormatName; },
        },
        raw);
}

std::pair<Hash, OutputHashFormat>
OutputHashFormat::parseAnyReturningFormat(std::string_view s, std::optional<HashAlgorithm> optAlgo)
{
    /* Try parsing as JSON first. If it is valid JSON, it must also be
       in the right format. Otherwise, parse string formats. */
    std::optional<nlohmann::json> jsonOpt;
    try {
        jsonOpt = nlohmann::json::parse(s);
    } catch (nlohmann::json::parse_error &) {
    }

    if (jsonOpt) {
        auto hash = jsonOpt->get<Hash>();
        if (optAlgo && hash.algo != *optAlgo)
            throw BadHash("hash '%s' should have type '%s'", s, printHashAlgo(*optAlgo));
        return {hash, OutputHashFormat::JSON};
    }

    auto [hash, format] = Hash::parseAnyReturningFormat(s, optAlgo);
    return {hash, format};
}

void printHash(const Hash & h, const OutputHashFormat & format, MixPrintJSON & printer)
{
    std::visit(
        overloaded{
            [&](const HashFormat & hf) { logger->cout(h.to_string(hf, hf == HashFormat::SRI)); },
            [&](const OutputFormatJSON &) { printer.printJSON(nlohmann::json(h)); },
        },
        format.raw);
}

} // namespace nix

namespace nix::flag {

static void hashFormatCompleter(AddCompletions & completions, size_t index, std::string_view prefix)
{
    for (auto & format : hashFormats) {
        if (hasPrefix(format, prefix)) {
            completions.add(format);
        }
    }
    auto jsonName = OutputHashFormat(OutputHashFormat::JSON).print();
    if (hasPrefix(jsonName, prefix)) {
        completions.add(std::string{jsonName});
    }
}

Args::Flag hashFormatWithDefault(std::string && longName, OutputHashFormat * hf)
{
    assert(*hf == nix::HashFormat::SRI);
    return Args::Flag{
        .longName = std::move(longName),
        .description = "Hash format (`base16`, `nix32`, `base64`, `sri`, `json-base16`). Default: `sri`.",
        .labels = {"hash-format"},
        .handler = {[hf](std::string s) { *hf = OutputHashFormat::parse(s); }},
        .completer = hashFormatCompleter,
    };
}

Args::Flag hashFormatOpt(std::string && longName, std::optional<OutputHashFormat> * ohf)
{
    return Args::Flag{
        .longName = std::move(longName),
        .description = "Hash format (`base16`, `nix32`, `base64`, `sri`, `json-base16`).",
        .labels = {"hash-format"},
        .handler = {[ohf](std::string s) { *ohf = std::optional<OutputHashFormat>{OutputHashFormat::parse(s)}; }},
        .completer = hashFormatCompleter,
    };
}

static void hashAlgoCompleter(AddCompletions & completions, size_t index, std::string_view prefix)
{
    for (auto & algo : hashAlgorithms)
        if (hasPrefix(algo, prefix))
            completions.add(algo);
}

Args::Flag hashAlgo(std::string && longName, HashAlgorithm * ha)
{
    return Args::Flag{
        .longName = std::move(longName),
        .description = "Hash algorithm (`blake3`, `md5`, `sha1`, `sha256`, or `sha512`).",
        .labels = {"hash-algo"},
        .handler = {[ha](std::string s) { *ha = parseHashAlgo(s); }},
        .completer = hashAlgoCompleter,
    };
}

Args::Flag hashAlgoOpt(std::string && longName, std::optional<HashAlgorithm> * oha)
{
    return Args::Flag{
        .longName = std::move(longName),
        .description =
            "Hash algorithm (`blake3`, `md5`, `sha1`, `sha256`, or `sha512`). Can be omitted for SRI hashes.",
        .labels = {"hash-algo"},
        .handler = {[oha](std::string s) { *oha = std::optional<HashAlgorithm>{parseHashAlgo(s)}; }},
        .completer = hashAlgoCompleter,
    };
}

Args::Flag fileIngestionMethod(FileIngestionMethod * method)
{
    return Args::Flag{
        .longName = "mode",
        // FIXME indentation carefully made for context, this is messed up.
        .description = R"(
    How to compute the hash of the input.
    One of:

    - `nar` (the default):
      Serialises the input as a
      [Nix Archive](@docroot@/store/file-system-object/content-address.md#serial-nix-archive)
      and passes that to the hash function.

    - `flat`:
      Assumes that the input is a single file and
      [directly passes](@docroot@/store/file-system-object/content-address.md#serial-flat)
      it to the hash function.
        )",
        .labels = {"file-ingestion-method"},
        .handler = {[method](std::string s) { *method = parseFileIngestionMethod(s); }},
    };
}

Args::Flag contentAddressMethod(ContentAddressMethod * method)
{
    return Args::Flag{
        .longName = "mode",
        // FIXME indentation carefully made for context, this is messed up.
        .description = R"(
    How to compute the content-address of the store object.
    One of:

    - [`nar`](@docroot@/store/store-object/content-address.md#method-nix-archive)
      (the default):
      Serialises the input as a
      [Nix Archive](@docroot@/store/file-system-object/content-address.md#serial-nix-archive)
      and passes that to the hash function.

    - [`flat`](@docroot@/store/store-object/content-address.md#method-flat):
      Assumes that the input is a single file and
      [directly passes](@docroot@/store/file-system-object/content-address.md#serial-flat)
      it to the hash function.

    - [`text`](@docroot@/store/store-object/content-address.md#method-text):
      Like `flat`, but used for
      [derivations](@docroot@/glossary.md#gloss-store-derivation) serialized in store object and
      [`builtins.toFile`](@docroot@/language/builtins.html#builtins-toFile).
      For advanced use-cases only;
      for regular usage prefer `nar` and `flat`.
        )",
        .labels = {"content-address-method"},
        .handler = {[method](std::string s) { *method = ContentAddressMethod::parse(s); }},
    };
}

} // namespace nix::flag

#include "misc-store-flags.hh"

namespace nix::flag
{

static void hashFormatCompleter(AddCompletions & completions, size_t index, std::string_view prefix)
{
    for (auto & format : hashFormats) {
        if (hasPrefix(format, prefix)) {
            completions.add(format);
        }
    }
}

Args::Flag hashFormatWithDefault(std::string && longName, HashFormat * hf)
{
    assert(*hf == nix::HashFormat::SRI);
    return Args::Flag {
            .longName = std::move(longName),
            .description = "Hash format (`base16`, `nix32`, `base64`, `sri`). Default: `sri`.",
            .labels = {"hash-format"},
            .handler = {[hf](std::string s) {
                *hf = parseHashFormat(s);
            }},
            .completer = hashFormatCompleter,
    };
}

Args::Flag hashFormatOpt(std::string && longName, std::optional<HashFormat> * ohf)
{
    return Args::Flag {
            .longName = std::move(longName),
            .description = "Hash format (`base16`, `nix32`, `base64`, `sri`).",
            .labels = {"hash-format"},
            .handler = {[ohf](std::string s) {
                *ohf = std::optional<HashFormat>{parseHashFormat(s)};
            }},
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
    return Args::Flag {
            .longName = std::move(longName),
            .description = "Hash algorithm (`md5`, `sha1`, `sha256`, or `sha512`).",
            .labels = {"hash-algo"},
            .handler = {[ha](std::string s) {
                *ha = parseHashAlgo(s);
            }},
            .completer = hashAlgoCompleter,
    };
}

Args::Flag hashAlgoOpt(std::string && longName, std::optional<HashAlgorithm> * oha)
{
    return Args::Flag {
            .longName = std::move(longName),
            .description = "Hash algorithm (`md5`, `sha1`, `sha256`, or `sha512`). Can be omitted for SRI hashes.",
            .labels = {"hash-algo"},
            .handler = {[oha](std::string s) {
                *oha = std::optional<HashAlgorithm>{parseHashAlgo(s)};
            }},
            .completer = hashAlgoCompleter,
    };
}

Args::Flag fileIngestionMethod(FileIngestionMethod * method)
{
    return Args::Flag {
        .longName  = "mode",
        // FIXME indentation carefully made for context, this is messed up.
        .description = R"(
    How to compute the hash of the input.
    One of:

    - `nar` (the default): Serialises the input as an archive (following the [_Nix Archive Format_](https://edolstra.github.io/pubs/phd-thesis.pdf#page=101)) and passes that to the hash function.

    - `flat`: Assumes that the input is a single file and directly passes it to the hash function;
        )",
        .labels = {"file-ingestion-method"},
        .handler = {[method](std::string s) {
            *method = parseFileIngestionMethod(s);
        }},
    };
}

Args::Flag contentAddressMethod(ContentAddressMethod * method)
{
    return Args::Flag {
        .longName  = "mode",
        // FIXME indentation carefully made for context, this is messed up.
        .description = R"(
    How to compute the content-address of the store object.
    One of:

    - `nar` (the default): Serialises the input as an archive (following the [_Nix Archive Format_](https://edolstra.github.io/pubs/phd-thesis.pdf#page=101)) and passes that to the hash function.

    - `flat`: Assumes that the input is a single file and directly passes it to the hash function;

    - `text`: Like `flat`, but used for
      [derivations](@docroot@/glossary.md#store-derivation) serialized in store object and 
      [`builtins.toFile`](@docroot@/language/builtins.html#builtins-toFile).
      For advanced use-cases only;
      for regular usage prefer `nar` and `flat.
        )",
        .labels = {"content-address-method"},
        .handler = {[method](std::string s) {
            *method = ContentAddressMethod::parse(s);
        }},
    };
}

}

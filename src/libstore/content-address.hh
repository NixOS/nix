#pragma once
///@file

#include <variant>
#include "hash.hh"
#include "comparator.hh"

namespace nix {

/**
 * An enumeration of the ways we can serialize file system objects.
 */
enum struct FileIngestionMethod : uint8_t {
    /**
     * Flat-file hashing. Directly ingest the contents of a single file
     */
    Flat = false,
    /**
     * Recursive (or NAR) hashing. Serializes the file-system object in Nix
     * Archive format and ingest that
     */
    Recursive = true
};

/**
 * Somewhat obscure, used by \ref Derivation derivations and
 * `builtins.toFile` currently.
 */
struct TextHash {
    /**
     * Hash of the contents of the text/file.
     */
    Hash hash;

    GENERATE_CMP(TextHash, me->hash);
};

/**
 * For path computed by makeFixedOutputPath.
 */
struct FixedOutputHash {
    /**
     * How the file system objects are serialized
     */
    FileIngestionMethod method;
    /**
     * Hash of that serialization
     */
    Hash hash;

    std::string printMethodAlgo() const;

    GENERATE_CMP(FixedOutputHash, me->method, me->hash);
};

/**
 * We've accumulated several types of content-addressed paths over the
 * years; fixed-output derivations support multiple hash algorithms and
 * serialisation methods (flat file vs NAR). Thus, ‘ca’ has one of the
 * following forms:
 *
 * - ‘text:sha256:<sha256 hash of file contents>’: For paths
 *   computed by Store::makeTextPath() / Store::addTextToStore().
 *
 * - ‘fixed:<r?>:<ht>:<h>’: For paths computed by
 *   Store::makeFixedOutputPath() / Store::addToStore().
 */
typedef std::variant<
    TextHash,
    FixedOutputHash
> ContentAddress;

/**
 * Compute the prefix to the hash algorithm which indicates how the
 * files were ingested.
 */
std::string makeFileIngestionPrefix(const FileIngestionMethod m);

/**
 * Compute the content-addressability assertion (ValidPathInfo::ca) for
 * paths created by Store::makeFixedOutputPath() / Store::addToStore().
 */
std::string makeFixedOutputCA(FileIngestionMethod method, const Hash & hash);

std::string renderContentAddress(ContentAddress ca);

std::string renderContentAddress(std::optional<ContentAddress> ca);

ContentAddress parseContentAddress(std::string_view rawCa);

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt);

Hash getContentAddressHash(const ContentAddress & ca);

/*
  We only have one way to hash text with references, so this is single-value
  type is only useful in std::variant.
*/
struct TextHashMethod { };
struct FixedOutputHashMethod {
  FileIngestionMethod fileIngestionMethod;
  HashType hashType;
};

/**
 * Ways of content addressing but not a complete ContentAddress.
 *
 * A ContentAddress without a Hash.
 */
typedef std::variant<
    TextHashMethod,
    FixedOutputHashMethod
  > ContentAddressMethod;

ContentAddressMethod parseContentAddressMethod(std::string_view rawCaMethod);

std::string renderContentAddressMethod(ContentAddressMethod caMethod);

}

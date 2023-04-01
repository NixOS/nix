#pragma once

#include <variant>
#include "hash.hh"
#include "path.hh"
#include "comparator.hh"

namespace nix {

/*
 * Content addressing method
 */

/* We only have one way to hash text with references, so this is a single-value
   type, mainly useful with std::variant.
*/

/**
 * The single way we can serialize "text" file system objects.
 *
 * Somewhat obscure, used by \ref Derivation derivations and
 * `builtins.toFile` currently.
 */
struct TextHashMethod : std::monostate { };

/**
 * An enumeration of the main ways we can serialize file system
 * objects.
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
 * Compute the prefix to the hash algorithm which indicates how the
 * files were ingested.
 */
std::string makeFileIngestionPrefix(FileIngestionMethod m);


/**
 * An enumeration of all the ways we can serialize file system objects.
 *
 * Just the type of a content address. Combine with the hash itself, and
 * we have a `ContentAddress` as defined below. Combine that, in turn,
 * with info on references, and we have `ContentAddressWithReferences`,
 * as defined further below.
 */
typedef std::variant<
    TextHashMethod,
    FileIngestionMethod
> ContentAddressMethod;

/* Parse and pretty print the algorithm which indicates how the files
   were ingested, with the the fixed output case not prefixed for back
   compat. */

std::string makeContentAddressingPrefix(ContentAddressMethod m);

ContentAddressMethod parseContentAddressingPrefix(std::string_view & m);

/* Parse and pretty print a content addressing method and hash in a
   nicer way, prefixing both cases. */

std::string renderContentAddressMethodAndHash(ContentAddressMethod cam, HashType ht);

std::pair<ContentAddressMethod, HashType> parseContentAddressMethod(std::string_view caMethod);


/*
 * Mini content address
 */

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
 * Used by most store objects that are content-addressed.
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
 * Compute the content-addressability assertion (ValidPathInfo::ca) for
 * paths created by Store::makeFixedOutputPath() / Store::addToStore().
 */
std::string renderContentAddress(ContentAddress ca);

std::string renderContentAddress(std::optional<ContentAddress> ca);

ContentAddress parseContentAddress(std::string_view rawCa);

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt);

Hash getContentAddressHash(const ContentAddress & ca);


/**
 * A set of references to other store objects.
 *
 * References to other store objects are tracked with store paths, self
 * references however are tracked with a boolean.
 */
struct StoreReferences {
    /**
     * References to other store objects
     */
    StorePathSet others;

    /**
     * Reference to this store object
     */
    bool self = false;

    /**
     * @return true iff no references, i.e. others is empty and self is
     * false.
     */
    bool empty() const;

    /**
     * Returns the numbers of references, i.e. the size of others + 1
     * iff self is true.
     */
    size_t size() const;

    GENERATE_CMP(StoreReferences, me->self, me->others);
};

/*
 * Full content address
 *
 * See the schema for store paths in store-api.cc
 */

// This matches the additional info that we need for makeTextPath
struct TextInfo {
    TextHash hash;
    /**
     * References to other store objects only; self references
     * disallowed
     */
    StorePathSet references;

    GENERATE_CMP(TextInfo, me->hash, me->references);
};

struct FixedOutputInfo {
    FixedOutputHash hash;
    /**
     * References to other store objects or this one.
     */
    StoreReferences references;

    GENERATE_CMP(FixedOutputInfo, me->hash, me->references);
};

/**
 * Ways of content addressing but not a complete ContentAddress.
 *
 * A ContentAddress without a Hash.
 */
typedef std::variant<
    TextInfo,
    FixedOutputInfo
> ContentAddressWithReferences;

/**
 * Create a ContentAddressWithReferences from a mere ContentAddress, by
 * assuming no references in all cases.
 */
ContentAddressWithReferences caWithoutRefs(const ContentAddress &);

ContentAddressWithReferences contentAddressFromMethodHashAndRefs(
    ContentAddressMethod method, Hash && hash, StoreReferences && refs);

ContentAddressMethod getContentAddressMethod(const ContentAddressWithReferences & ca);
Hash getContentAddressHash(const ContentAddressWithReferences & ca);

std::string printMethodAlgo(const ContentAddressWithReferences &);

}

#pragma once
///@file

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
 *
 * TextIngestionMethod is identical to FileIngestionMethod::Fixed except that
 * the former may not have self-references and is tagged `text:${algo}:${hash}`
 * rather than `fixed:${algo}:${hash}`.  The contents of the store path are
 * ingested and hashed identically, aside from the slightly different tag and
 * restriction on self-references.
 */
struct TextIngestionMethod : std::monostate { };

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
struct ContentAddressMethod
{
    typedef std::variant<
        TextIngestionMethod,
        FileIngestionMethod
    > Raw;

    Raw raw;

    GENERATE_CMP(ContentAddressMethod, me->raw);

    /* The moral equivalent of `using Raw::Raw;` */
    ContentAddressMethod(auto &&... arg)
        : raw(std::forward<decltype(arg)>(arg)...)
    { }


    /**
     * Parse the prefix tag which indicates how the files
     * were ingested, with the fixed output case not prefixed for back
     * compat.
     *
     * @param [in] m A string that should begin with the prefix.
     * @param [out] m The remainder of the string after the prefix.
     */
    static ContentAddressMethod parsePrefix(std::string_view & m);

    /**
     * Render the prefix tag which indicates how the files wre ingested.
     *
     * The rough inverse of `parsePrefix()`.
     */
    std::string renderPrefix() const;

    /**
     * Parse a content addressing method and hash type.
     */
    static std::pair<ContentAddressMethod, HashType> parse(std::string_view rawCaMethod);

    /**
     * Render a content addressing method and hash type in a
     * nicer way, prefixing both cases.
     *
     * The rough inverse of `parse()`.
     */
    std::string render(HashType ht) const;
};


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
struct ContentAddress
{
    typedef std::variant<
        TextHash,
        FixedOutputHash
    > Raw;

    Raw raw;

    GENERATE_CMP(ContentAddress, me->raw);

    /* The moral equivalent of `using Raw::Raw;` */
    ContentAddress(auto &&... arg)
        : raw(std::forward<decltype(arg)>(arg)...)
    { }

    /**
     * Compute the content-addressability assertion
     * (`ValidPathInfo::ca`) for paths created by
     * `Store::makeFixedOutputPath()` / `Store::addToStore()`.
     */
    std::string render() const;

    static ContentAddress parse(std::string_view rawCa);

    static std::optional<ContentAddress> parseOpt(std::string_view rawCaOpt);

    /**
     * Create a `ContentAddress` from 2 parts:
     *
     * @param method Way ingesting the file system data.
     *
     * @param hash Hash of ingested file system data.
     */
    static ContentAddress fromParts(
        ContentAddressMethod method, Hash hash) noexcept;

    ContentAddressMethod getMethod() const;

    const Hash & getHash() const;

    std::string printMethodAlgo() const;
};

/**
 * Render the `ContentAddress` if it exists to a string, return empty
 * string otherwise.
 */
std::string renderContentAddress(std::optional<ContentAddress> ca);


/*
 * Full content address
 *
 * See the schema for store paths in store-api.cc
 */

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
struct ContentAddressWithReferences
{
    typedef std::variant<
        TextInfo,
        FixedOutputInfo
    > Raw;

    Raw raw;

    GENERATE_CMP(ContentAddressWithReferences, me->raw);

    /* The moral equivalent of `using Raw::Raw;` */
    ContentAddressWithReferences(auto &&... arg)
        : raw(std::forward<decltype(arg)>(arg)...)
    { }

    /**
     * Create a `ContentAddressWithReferences` from a mere
     * `ContentAddress`, by claiming no references.
     */
    static ContentAddressWithReferences withoutRefs(const ContentAddress &) noexcept;

    /**
     * Create a `ContentAddressWithReferences` from 3 parts:
     *
     * @param method Way ingesting the file system data.
     *
     * @param hash Hash of ingested file system data.
     *
     * @param refs References to other store objects or oneself.
     *
     * Do note that not all combinations are supported; `nullopt` is
     * returns for invalid combinations.
     */
    static std::optional<ContentAddressWithReferences> fromPartsOpt(
        ContentAddressMethod method, Hash hash, StoreReferences refs) noexcept;

    ContentAddressMethod getMethod() const;

    Hash getHash() const;
};

}

#pragma once
///@file

#include <variant>
#include "hash.hh"
#include "path.hh"
#include "file-content-address.hh"
#include "comparator.hh"
#include "variant-wrapper.hh"

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
 * Compute the prefix to the hash algorithm which indicates how the
 * files were ingested.
 */
std::string_view makeFileIngestionPrefix(FileIngestionMethod m);

/**
 * An enumeration of all the ways we can content-address store objects.
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

    MAKE_WRAPPER_CONSTRUCTOR(ContentAddressMethod);

    /**
     * Parse a content addressing method (name).
     *
     * The inverse of `render`.
     */
    static ContentAddressMethod parse(std::string_view rawCaMethod);

    /**
     * Render a content addressing method (name).
     *
     * The inverse of `parse`.
     */
    std::string_view render() const;

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
    std::string_view renderPrefix() const;

    /**
     * Parse a content addressing method and hash algorithm.
     */
    static std::pair<ContentAddressMethod, HashAlgorithm> parseWithAlgo(std::string_view rawCaMethod);

    /**
     * Render a content addressing method and hash algorithm in a
     * nicer way, prefixing both cases.
     *
     * The rough inverse of `parse()`.
     */
    std::string renderWithAlgo(HashAlgorithm ha) const;

    /**
     * Get the underlying way to content-address file system objects.
     *
     * Different ways of hashing store objects may use the same method
     * for hashing file systeme objects.
     */
    FileIngestionMethod getFileIngestionMethod() const;
};


/*
 * Mini content address
 */

/**
 * We've accumulated several types of content-addressed paths over the
 * years; fixed-output derivations support multiple hash algorithms and
 * serialisation methods (flat file vs NAR). Thus, ‘ca’ has one of the
 * following forms:
 *
 * - `TextIngestionMethod`:
 *   ‘text:sha256:<sha256 hash of file contents>’
 *
 * - `FixedIngestionMethod`:
 *   ‘fixed:<r?>:<hash algorithm>:<hash of file contents>’
 */
struct ContentAddress
{
    /**
     * How the file system objects are serialized
     */
    ContentAddressMethod method;

    /**
     * Hash of that serialization
     */
    Hash hash;

    GENERATE_CMP(ContentAddress, me->method, me->hash);

    /**
     * Compute the content-addressability assertion
     * (`ValidPathInfo::ca`) for paths created by
     * `Store::makeFixedOutputPath()` / `Store::addToStore()`.
     */
    std::string render() const;

    static ContentAddress parse(std::string_view rawCa);

    static std::optional<ContentAddress> parseOpt(std::string_view rawCaOpt);

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
struct StoreReferences
{
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
struct TextInfo
{
    /**
     * Hash of the contents of the text/file.
     */
    Hash hash;

    /**
     * References to other store objects only; self references
     * disallowed
     */
    StorePathSet references;

    GENERATE_CMP(TextInfo, me->hash, me->references);
};

struct FixedOutputInfo
{
    /**
     * How the file system objects are serialized
     */
    FileIngestionMethod method;

    /**
     * Hash of that serialization
     */
    Hash hash;

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

    MAKE_WRAPPER_CONSTRUCTOR(ContentAddressWithReferences);

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
     * @note note that all combinations are supported. This is a
     * *partial function* and exceptions will be thrown for invalid
     * combinations.
     */
    static ContentAddressWithReferences fromParts(
        ContentAddressMethod method, Hash hash, StoreReferences refs);

    ContentAddressMethod getMethod() const;

    Hash getHash() const;
};

}

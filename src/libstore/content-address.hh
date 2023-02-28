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
struct TextHashMethod : std::monostate { };

enum struct FileIngestionMethod : uint8_t {
    Flat = false,
    Recursive = true
};

struct FixedOutputHashMethod {
  FileIngestionMethod fileIngestionMethod;
  HashType hashType;
};

/* Compute the prefix to the hash algorithm which indicates how the files were
   ingested. */
std::string makeFileIngestionPrefix(FileIngestionMethod m);


/* Just the type of a content address. Combine with the hash itself, and we
   have a `ContentAddress` as defined below. Combine that, in turn, with info
   on references, and we have `ContentAddressWithReferences`, as defined
   further below. */
typedef std::variant<
    TextHashMethod,
    FixedOutputHashMethod
> ContentAddressMethod;

ContentAddressMethod parseContentAddressMethod(std::string_view rawCaMethod);

std::string renderContentAddressMethod(ContentAddressMethod caMethod);

/*
 * Mini content address
 */

struct TextHash {
    Hash hash;

    GENERATE_CMP(TextHash, me->hash);
};

/// Pair of a hash, and how the file system was ingested
struct FixedOutputHash {
    FileIngestionMethod method;
    Hash hash;
    std::string printMethodAlgo() const;

    GENERATE_CMP(FixedOutputHash, me->method, me->hash);
};

/*
  We've accumulated several types of content-addressed paths over the years;
  fixed-output derivations support multiple hash algorithms and serialisation
  methods (flat file vs NAR). Thus, ‘ca’ has one of the following forms:

  * ‘text:sha256:<sha256 hash of file contents>’: For paths
    computed by makeTextPath() / addTextToStore().

  * ‘fixed:<r?>:<ht>:<h>’: For paths computed by
    makeFixedOutputPath() / addToStore().
*/
typedef std::variant<
    TextHash, // for paths computed by makeTextPath() / addTextToStore
    FixedOutputHash // for path computed by makeFixedOutputPath
> ContentAddress;

std::string renderContentAddress(ContentAddress ca);

std::string renderContentAddress(std::optional<ContentAddress> ca);

ContentAddress parseContentAddress(std::string_view rawCa);

std::optional<ContentAddress> parseContentAddressOpt(std::string_view rawCaOpt);

Hash getContentAddressHash(const ContentAddress & ca);


/*
 * References set
 */

struct StoreReferences {
    StorePathSet others;
    bool self = false;

    bool empty() const;
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
    // References for the paths, self references disallowed
    StorePathSet references;

    GENERATE_CMP(TextInfo, me->hash, me->references);
};

struct FixedOutputInfo {
    FixedOutputHash hash;
    // References for the paths
    StoreReferences references;

    GENERATE_CMP(FixedOutputInfo, me->hash, me->references);
};

typedef std::variant<
    TextInfo,
    FixedOutputInfo
> ContentAddressWithReferences;

ContentAddressWithReferences caWithoutRefs(const ContentAddress &);

}

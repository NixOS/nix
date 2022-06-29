#pragma once

#include <variant>
#include "hash.hh"

namespace nix {

enum struct FileIngestionMethod : uint8_t { Flat = false, Recursive = true };

struct TextHash
{
    Hash hash;
};

/// Pair of a hash, and how the file system was ingested
struct FixedOutputHash
{
    FileIngestionMethod method;
    Hash hash;
    std::string printMethodAlgo() const;
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
    TextHash,       // for paths computed by makeTextPath() / addTextToStore
    FixedOutputHash // for path computed by makeFixedOutputPath
    >
    ContentAddress;

/* Compute the prefix to the hash algorithm which indicates how the files were
   ingested. */
std::string makeFileIngestionPrefix(const FileIngestionMethod m);

/* Compute the content-addressability assertion (ValidPathInfo::ca)
   for paths created by makeFixedOutputPath() / addToStore(). */
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
struct TextHashMethod
{};
struct FixedOutputHashMethod
{
    FileIngestionMethod fileIngestionMethod;
    HashType hashType;
};

typedef std::variant<TextHashMethod, FixedOutputHashMethod> ContentAddressMethod;

ContentAddressMethod parseContentAddressMethod(std::string_view rawCaMethod);

std::string renderContentAddressMethod(ContentAddressMethod caMethod);

}

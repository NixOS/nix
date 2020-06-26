#pragma once

#include <variant>
#include "hash.hh"
#include "path.hh"

namespace nix {

/*
 * Mini content address
 */

enum struct FileIngestionMethod : uint8_t {
    Flat = false,
    Recursive = true
};

struct TextHash {
    Hash hash;
};

/// Pair of a hash, and how the file system was ingested
struct FixedOutputHash {
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
    TextHash, // for paths computed by makeTextPath() / addTextToStore
    FixedOutputHash // for path computed by makeFixedOutputPath
> MiniContentAddress;

/* Compute the prefix to the hash algorithm which indicates how the files were
   ingested. */
std::string makeFileIngestionPrefix(const FileIngestionMethod m);

std::string renderMiniContentAddress(MiniContentAddress ca);

std::string renderMiniContentAddress(std::optional<MiniContentAddress> ca);

MiniContentAddress parseMiniContentAddress(std::string_view rawCa);

std::optional<MiniContentAddress> parseMiniContentAddressOpt(std::string_view rawCaOpt);

/*
 * References set
 */

template<typename Ref>
struct PathReferences
{
    std::set<Ref> references;
    bool hasSelfReference = false;

    /* Functions to view references + hasSelfReference as one set, mainly for
       compatibility's sake. */
    StorePathSet referencesPossiblyToSelf(const Ref & self) const;
    void insertReferencePossiblyToSelf(const Ref & self, Ref && ref);
    void setReferencesPossiblyToSelf(const Ref & self, std::set<Ref> && refs);
};

template<typename Ref>
StorePathSet PathReferences<Ref>::referencesPossiblyToSelf(const Ref & self) const
{
    StorePathSet references { references };
    if (hasSelfReference)
        references.insert(self);
    return references;
}

template<typename Ref>
void PathReferences<Ref>::insertReferencePossiblyToSelf(const Ref & self, Ref && ref)
{
    if (ref == self)
        hasSelfReference = true;
    else
        references.insert(std::move(ref));
}

template<typename Ref>
void PathReferences<Ref>::setReferencesPossiblyToSelf(const Ref & self, std::set<Ref> && refs)
{
    if (refs.count(self))
        hasSelfReference = true;
        refs.erase(self);

    references = refs;
}

/*
 * Full content address
 *
 * See the schema for store paths in store-api.cc
 */

// This matches the additional info that we need for makeTextPath
struct TextInfo : TextHash {
    // References for the paths, self references disallowed
    StorePathSet references;
};

struct FixedOutputInfo : FixedOutputHash {
    // References for the paths
    PathReferences<StorePath> references;
};

struct FullContentAddress {
    std::string name;
    std::variant<
        TextInfo,
        FixedOutputInfo
    > info;

    bool operator < (const FullContentAddress & other) const
    {
        return name < other.name;
    }

};

}

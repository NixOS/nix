#pragma once

#include "crypto.hh"
#include "path.hh"
#include "hash.hh"
#include "content-address.hh"

#include <string>
#include <optional>

namespace nix {


class Store;


struct SubstitutablePathInfo : PathReferences<StorePath>
{
    std::optional<StorePath> deriver;
    uint64_t downloadSize; /* 0 = unknown or inapplicable */
    uint64_t narSize; /* 0 = unknown */
};

typedef std::map<StorePath, SubstitutablePathInfo> SubstitutablePathInfos;

struct ValidPathInfo : PathReferences<StorePath>
{
    StorePath path;
    std::optional<StorePath> deriver;
    Hash narHash;
    time_t registrationTime = 0;
    uint64_t narSize = 0; // 0 = unknown
    uint64_t id; // internal use only

    /* Whether the path is ultimately trusted, that is, it's a
       derivation output that was built locally. */
    bool ultimate = false;

    StringSet sigs; // note: not necessarily verified

    /* If non-empty, an assertion that the path is content-addressed,
       i.e., that the store path is computed from a cryptographic hash
       of the contents of the path, plus some other bits of data like
       the "name" part of the path. Such a path doesn't need
       signatures, since we don't have to trust anybody's claim that
       the path is the output of a particular derivation. (In the
       extensional store model, we have to trust that the *contents*
       of an output path of a derivation were actually produced by
       that derivation. In the intensional model, we have to trust
       that a particular output path was produced by a derivation; the
       path then implies the contents.)

       Ideally, the content-addressability assertion would just be a Boolean,
       and the store path would be computed from the name component, ‘narHash’
       and ‘references’. However, we support many types of content addresses.
    */
    std::optional<ContentAddress> ca;

    bool operator == (const ValidPathInfo & i) const
    {
        return
            path == i.path
            && narHash == i.narHash
            && hasSelfReference == i.hasSelfReference
            && references == i.references;
    }

    /* Return a fingerprint of the store path to be used in binary
       cache signatures. It contains the store path, the base-32
       SHA-256 hash of the NAR serialisation of the path, the size of
       the NAR, and the sorted references. The size field is strictly
       speaking superfluous, but might prevent endless/excessive data
       attacks. */
    std::string fingerprint(const Store & store) const;

    void sign(const Store & store, const SecretKey & secretKey);

    std::optional<StorePathDescriptor> fullStorePathDescriptorOpt() const;

    /* Return true iff the path is verifiably content-addressed. */
    bool isContentAddressed(const Store & store) const;

    /* Functions to view references + hasSelfReference as one set, mainly for
       compatibility's sake. */
    StorePathSet referencesPossiblyToSelf() const;
    void insertReferencePossiblyToSelf(StorePath && ref);
    void setReferencesPossiblyToSelf(StorePathSet && refs);

    static const size_t maxSigs = std::numeric_limits<size_t>::max();

    /* Return the number of signatures on this .narinfo that were
       produced by one of the specified keys, or maxSigs if the path
       is content-addressed. */
    size_t checkSignatures(const Store & store, const PublicKeys & publicKeys) const;

    /* Verify a single signature. */
    bool checkSignature(const Store & store, const PublicKeys & publicKeys, const std::string & sig) const;

    Strings shortRefs() const;

    ValidPathInfo(const ValidPathInfo & other) = default;

    ValidPathInfo(StorePath && path, Hash narHash) : path(std::move(path)), narHash(narHash) { };
    ValidPathInfo(const StorePath & path, Hash narHash) : path(path), narHash(narHash) { };

    ValidPathInfo(const Store & store,
        StorePathDescriptor && ca, Hash narHash);

    virtual ~ValidPathInfo() { }
};

typedef list<ValidPathInfo> ValidPathInfos;

}

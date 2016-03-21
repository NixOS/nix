#pragma once

#include "types.hh"
#include "hash.hh"
#include "store-api.hh"

namespace nix {

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    Hash fileHash;
    uint64_t fileSize = 0;
    std::string system;

    NarInfo() { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const std::string & s, const std::string & whence);

    std::string to_string() const;

    /*  Return a fingerprint of the store path to be used in binary
        cache signatures. It contains the store path, the base-32
        SHA-256 hash of the NAR serialisation of the path, the size of
        the NAR, and the sorted references. The size field is strictly
        speaking superfluous, but might prevent endless/excessive data
        attacks. */
    std::string fingerprint() const;

    void sign(const SecretKey & secretKey);

    /* Return the number of signatures on this .narinfo that were
       produced by one of the specified keys. */
    unsigned int checkSignatures(const PublicKeys & publicKeys) const;

private:

    Strings shortRefs() const;
};

}

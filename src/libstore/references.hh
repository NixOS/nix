#pragma once

#include "hash.hh"
#include "path.hh"

namespace nix {

std::pair<StorePathSet, HashResult> scanForReferences(const Path & path, const StorePathSet & refs);

StorePathSet scanForReferences(Sink & toTee, const Path & path, const StorePathSet & refs);

class RefScanSink : public Sink
{
    StringSet hashes;
    StringSet seen;

    std::string tail;

public:

    RefScanSink(StringSet && hashes) : hashes(hashes)
    { }

    StringSet & getResult()
    { return seen; }

    void operator () (std::string_view data) override;
};

struct RewritingSink : Sink
{
    std::string from, to, prev;
    Sink & nextSink;
    uint64_t pos = 0;

    std::vector<uint64_t> matches;

    RewritingSink(const std::string & from, const std::string & to, Sink & nextSink);

    void operator () (std::string_view data) override;

    void flush();
};

struct HashModuloSink : AbstractHashSink
{
    HashSink hashSink;
    RewritingSink rewritingSink;

    HashModuloSink(HashType ht, const std::string & modulus);

    void operator () (std::string_view data) override;

    HashResult finish() override;
};

}

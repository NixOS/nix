#pragma once

#include "types.hh"
#include "hash.hh"

namespace nix {

PathSet scanForReferences(PathView path, const PathSet & refs,
    HashResult & hash);

struct RewritingSink : Sink
{
    std::string from, to, prev;
    Sink & nextSink;
    uint64_t pos = 0;

    std::vector<uint64_t> matches;

    RewritingSink(std::string_view from, std::string_view to, Sink & nextSink);

    void operator () (const unsigned char * data, size_t len) override;

    void flush();
};

struct HashModuloSink : AbstractHashSink
{
    HashSink hashSink;
    RewritingSink rewritingSink;

    HashModuloSink(HashType ht, std::string_view modulus);

    void operator () (const unsigned char * data, size_t len) override;

    HashResult finish() override;
};

}

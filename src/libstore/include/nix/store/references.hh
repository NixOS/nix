#pragma once
///@file

#include "nix/util/hash.hh"

namespace nix {

class RefScanSink : public Sink
{
    StringSet hashes;
    StringSet seen;

    std::string tail;

    void anchor() override;

public:

    RefScanSink(StringSet && hashes)
        : hashes(hashes)
    {
    }

    StringSet & getResult()
    {
        return seen;
    }

    void operator()(std::string_view data) override;
};

struct RewritingSink : Sink
{
private:
    void anchor() override;

public:
    const StringMap rewrites;
    std::string::size_type maxRewriteSize;
    std::string prev;
    Sink & nextSink;
    uint64_t pos = 0;

    std::set<uint64_t> matches;

    RewritingSink(const std::string & from, const std::string & to, Sink & nextSink);
    RewritingSink(const StringMap & rewrites, Sink & nextSink);

    void operator()(std::string_view data) override;

    void flush();
};

struct HashModuloSink : AbstractHashSink
{
private:
    void anchor() override;

public:
    HashSink hashSink;
    RewritingSink rewritingSink;

    HashModuloSink(HashAlgorithm ha, const std::string & modulus);

    void operator()(std::string_view data) override;

    HashResult finish() override;
};

} // namespace nix

#include <regex>

#include <rapidcheck.h>

#include "hash.hh"

#include "tests/hash.hh"

namespace rc {
using namespace nix;

Gen<Hash> Arbitrary<Hash>::arbitrary()
{
    Hash hash(htSHA1);
    for (size_t i = 0; i < hash.hashSize; ++i)
        hash.hash[i] = *gen::arbitrary<uint8_t>();
    return gen::just(hash);
}

}

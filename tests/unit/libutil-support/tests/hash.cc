#include <regex>

#include <rapidcheck.h>

#include "hash.hh"

#include "tests/hash.hh"

namespace rc {
using namespace nix;

Gen<Hash> Arbitrary<Hash>::arbitrary()
{
    Hash prototype(HashAlgorithm::SHA1);
    return
        gen::apply(
            [](const std::vector<uint8_t> & v) {
                Hash hash(HashAlgorithm::SHA1);
                assert(v.size() == hash.hashSize);
                std::copy(v.begin(), v.end(), hash.hash);
                return hash;
            },
            gen::container<std::vector<uint8_t>>(prototype.hashSize, gen::arbitrary<uint8_t>())
        );
}

}

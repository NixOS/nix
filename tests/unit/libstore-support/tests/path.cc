#include <regex>

#include <rapidcheck.h>

#include "path-regex.hh"
#include "store-api.hh"

#include "tests/hash.hh"
#include "tests/path.hh"

namespace nix {

void showValue(const StorePath & p, std::ostream & os)
{
    os << p.to_string();
}

}

namespace rc {
using namespace nix;

Gen<StorePathName> Arbitrary<StorePathName>::arbitrary()
{
    auto len = *gen::inRange<size_t>(
        1,
        StorePath::MaxPathLen - StorePath::HashLen);

    std::string pre;
    pre.reserve(len);

    for (size_t c = 0; c < len; ++c) {
        switch (auto i = *gen::inRange<uint8_t>(0, 10 + 2 * 26 + 6)) {
            case 0 ... 9:
                pre += '0' + i;
            case 10 ... 35:
                pre += 'A' + (i - 10);
                break;
            case 36 ... 61:
                pre += 'a' + (i - 36);
                break;
            case 62:
                pre += '+';
                break;
            case 63:
                pre += '-';
                break;
            case 64:
                pre += '.';
                break;
            case 65:
                pre += '_';
                break;
            case 66:
                pre += '?';
                break;
            case 67:
                pre += '=';
                break;
            default:
                assert(false);
        }
    }

    return gen::just(StorePathName {
        .name = std::move(pre),
    });
}

Gen<StorePath> Arbitrary<StorePath>::arbitrary()
{
    return gen::just(StorePath {
        *gen::arbitrary<Hash>(),
        (*gen::arbitrary<StorePathName>()).name,
    });
}

} // namespace rc

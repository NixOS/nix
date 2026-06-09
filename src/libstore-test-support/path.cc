#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)

#include <rapidcheck/gen/Arbitrary.h>
#include <rapidcheck.h>

#include "nix/util/tests/hash.hh"
#include "nix/store/tests/path.hh"

namespace nix {

void showValue(const StorePath & p, std::ostream & os)
{
    os << p.to_string();
}

} // namespace nix

namespace rc {

Gen<char> storePathChar()
{
    return rc::gen::apply(
        [](uint8_t i) -> char {
            switch (i) {
            case 0 ... 9:
                return '0' + i;
            case 10 ... 35:
                return 'A' + (i - 10);
            case 36 ... 61:
                return 'a' + (i - 36);
            case 62:
                return '+';
            case 63:
                return '-';
            case 64:
                return '.';
            case 65:
                return '_';
            case 66:
                return '?';
            case 67:
                return '=';
            default:
                assert(false);
            }
        },
        gen::inRange<uint8_t>(0, 10 + 2 * 26 + 6));
}

Gen<nix::StorePathName> Arbitrary<nix::StorePathName>::arbitrary()
{
    return gen::construct<nix::StorePathName>(
        gen::suchThat(gen::container<std::string>(storePathChar()), [](const std::string & s) {
            return !(s == "" || s == "." || s == ".." || s.starts_with(".-") || s.starts_with("..-"));
        }));
}

Gen<StorePath> Arbitrary<nix::StorePath>::arbitrary()
{
    return gen::construct<nix::StorePath>(
        gen::arbitrary<nix::Hash>(),
        gen::apply([](nix::StorePathName n) { return n.name; }, gen::arbitrary<nix::StorePathName>()));
}

} // namespace rc

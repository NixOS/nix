#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)
#include <rapidcheck.h>

#include "nix/util/tests/filename.hh"

namespace rc {

Gen<nix::Filename> Arbitrary<nix::Filename>::arbitrary()
{
    using namespace nix;
    auto valid = [](const std::string & s) {
        try {
            FilenameView::fromStringThrowing(s);
            return true;
        } catch (BadFilename &) {
            return false;
        }
    };
    return gen::map(
        gen::weightedOneOf<std::string>({
            {4, gen::element<std::string>("a", "b", "c")},
            {1, gen::suchThat(gen::string<std::string>(), valid)},
        }),
        [](std::string s) { return Filename::fromStringAsserting(std::move(s)); });
}

} // namespace rc

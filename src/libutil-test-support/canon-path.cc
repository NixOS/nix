#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)
#include <rapidcheck.h>

#include "nix/util/tests/canon-path.hh"
#include "nix/util/tests/filename.hh"

namespace rc {

Gen<nix::CanonPath> Arbitrary<nix::CanonPath>::arbitrary()
{
    using namespace nix;
    return gen::map(
        gen::container<std::vector<Filename>>(gen::arbitrary<Filename>()), [](const std::vector<Filename> & names) {
            auto path = CanonPath::root;
            for (auto & name : names)
                path.push(name);
            return path;
        });
}

} // namespace rc

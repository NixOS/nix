#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)
#include <rapidcheck.h>

#include "nix/util/tests/filename.hh"
#include "nix/util/tests/unresolved-path.hh"

namespace nix::testing {

rc::Gen<std::string> rawPath(std::vector<std::string> names)
{
    names.insert(names.end(), {"..", ".", ""});
    return rc::gen::map(
        rc::gen::pair(
            rc::gen::arbitrary<bool>(),
            rc::gen::resize(8, rc::gen::container<std::vector<std::string>>(rc::gen::elementOf(names)))),
        [](const std::pair<bool, std::vector<std::string>> & p) {
            auto & [leadingSlash, components] = p;
            std::string res = leadingSlash ? "/" : "";
            for (size_t i = 0; i < components.size(); ++i) {
                if (i)
                    res += '/';
                res += components[i];
            }
            return res;
        });
}

} // namespace nix::testing

namespace rc {

Gen<nix::UnresolvedPath> Arbitrary<nix::UnresolvedPath>::arbitrary()
{
    using namespace nix;
    return gen::map(
        gen::container<std::vector<std::string>>(gen::weightedOneOf<std::string>({
            {3, gen::map(gen::arbitrary<Filename>(), [](const Filename & f) { return f.str(); })},
            {1, gen::just<std::string>("..")},
        })),
        [](const std::vector<std::string> & components) {
            /* Every component is a valid file name or `..`. */
            std::string path = "/";
            for (size_t i = 0; i < components.size(); ++i) {
                if (i)
                    path += '/';
                path += components[i];
            }
            return UnresolvedPath::fromUnchecked(std::move(path));
        });
}

} // namespace rc

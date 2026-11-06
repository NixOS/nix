#include "nix/store/derivation/aterm.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/store/derived-path.hh"
#include "nix/util/error.hh"
#include "nix/util/serialise.hh"

#include <cassert>
#include <string>
#include <string_view>
#include <variant>

namespace nix {

int fuzzParseDerivationCommon(std::string_view s, std::string storeDir = "/nix/store")
{
    nix::StringSource src{s};

    try {
        StoreDirConfig config{storeDir};
        auto drv = derivation::parse(config, std::string(s), "test");

        for (const auto & input : drv.inputs)
            if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw()))
                assert(built->getBaseStorePath().isDerivation());

        assert(derivation::unparse(drv, config) == s);
    } catch (const nix::Error &) {
    }
    return 0;
}

} // namespace nix

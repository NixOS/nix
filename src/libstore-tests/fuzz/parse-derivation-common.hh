#include "nix/store/derivation/aterm.hh"
#include "nix/store/store-dir-config.hh"

namespace nix {

int fuzzParseDerivationCommon(std::string_view s, std::string storeDir = "/nix/store")
{
    nix::StringSource src{s};

    try {
        StoreDirConfig config{storeDir};
        auto drv = derivation::parse(config, std::string(s), "test");

#if 0 /* This is broken now. See https://github.com/NixOS/nix/issues/16237. */
        for (const auto & [drvPath, _] : drv.inputs.drvs.map)
            assert(drvPath.isDerivation());
#endif

        /* TODO: Make sure all invariants are upheld and it round-trips with unparse(). */
    } catch (const nix::Error &) {
    }
    return 0;
}

} // namespace nix

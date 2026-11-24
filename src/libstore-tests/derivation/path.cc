#include <gtest/gtest.h>

#include "nix/store/derivation/path.hh"
#include "nix/store/derived-path-map.hh"
#include "nix/util/util.hh"

namespace nix {

static const StorePath drv{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"};
static const StorePath notDrv{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2"};

TEST(DerivationPath, acceptsDerivation)
{
    EXPECT_EQ(static_cast<const StorePath &>(DerivationPath::require(drv)), drv);
    EXPECT_EQ(DerivationPath::tryFrom(drv), DerivationPath::require(drv));
}

TEST(DerivationPath, rejectsNonDerivation)
{
    EXPECT_THROW(DerivationPath::require(notDrv), FormatError);
    EXPECT_EQ(DerivationPath::tryFrom(notDrv), std::nullopt);
}

/**
 * Looking up a root that is not a derivation is a miss, not an error:
 * such a path cannot be a key. Inserting one, though, is a caller
 * error.
 */
TEST(DerivationPath, derivedPathMapRoots)
{
    DerivedPathMap<StringSet> map;
    SingleDerivedPath::Opaque drvRoot{drv}, notDrvRoot{notDrv};

    map.ensureSlot(SingleDerivedPath{drvRoot}).value = {"out"};
    EXPECT_NE(map.findSlot(SingleDerivedPath{drvRoot}), nullptr);

    EXPECT_EQ(map.findSlot(SingleDerivedPath{notDrvRoot}), nullptr);
    EXPECT_THROW(map.ensureSlot(SingleDerivedPath{notDrvRoot}), FormatError);
}

} // namespace nix

#include <gtest/gtest.h>

#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derived-path.hh"

namespace nix {

/**
 * `fromSet` and `toSet` must be mutually inverse, in particular for
 * dynamic derivation inputs with more than one layer of
 * `SingleDerivedPath::Built`, where the orientation of the nested
 * `childMap` levels matters. (Depth 2 is symmetric and so would not
 * catch an orientation mistake.)
 */
TEST(FullInputs, fromSetRoundTripsDeeplyDynamicInputs)
{
    StorePath root{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-root.drv"};

    /* root.drv^a^b^c */
    SingleDerivedPath depth3 = SingleDerivedPath::Built{
        .drvPath = make_ref<SingleDerivedPath>(SingleDerivedPath::Built{
            .drvPath = make_ref<SingleDerivedPath>(SingleDerivedPath::Built{
                .drvPath = makeConstantStorePathRef(root),
                .output = "a",
            }),
            .output = "b",
        }),
        .output = "c",
    };

    std::set<SingleDerivedPath> inputs{
        SingleDerivedPath::Opaque{
            .path = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-src"},
        },
        depth3,
    };

    auto fullInputs = derivation::FullInputs::fromSet(inputs);

    /* The innermost output level must end up nearest the root:
       map[root].childMap["a"].childMap["b"].value = {"c"} */
    auto & rootNode = fullInputs.drvs.map.at(root);
    ASSERT_TRUE(rootNode.value.empty());
    auto & aNode = rootNode.childMap.at("a");
    ASSERT_TRUE(aNode.value.empty());
    auto & bNode = aNode.childMap.at("b");
    EXPECT_EQ(bNode.value, (std::set<OutputName, std::less<>>{"c"}));
    EXPECT_TRUE(bNode.childMap.empty());

    EXPECT_EQ(fullInputs.toSet(), inputs);
    EXPECT_EQ(derivation::FullInputs::fromSet(fullInputs.toSet()), fullInputs);
}

} // namespace nix

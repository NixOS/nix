#include <gtest/gtest.h>

#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/modulo.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix::derivation::modulo {

/**
 * Tests for `hash` and `hashInput`.
 *
 * The point of "hash modulo" is that derivations which differ only in
 * the *provenance* of their fixed-output inputs are indistinguishable:
 * two `fetchurl` calls with the same `outputHash` but different URLs
 * have different derivation paths, but everything downstream of them
 * should have the same output paths.
 *
 * Besides the derivations themselves, we characterize the intermediate
 * "modulo" ATerm --- the thing that is actually hashed --- so that the
 * input-addressing computation is reviewable, and not just a hash we
 * would have to take on faith.
 *
 * The graph, mirroring the one in issue #16307:
 *
 * - `source-first` and `source-second` are fixed-output derivations
 *   with the same output hash but different builders. Different
 *   derivations, same hash modulo.
 *
 * - `intermediate-first` and `intermediate-second` each depend on one
 *   of those, and have two outputs (`out` and `dev`). Again different
 *   derivations, same hash modulo.
 *
 * - `parent-split` takes `out` from one intermediate and `dev` from the
 *   other; `parent-joined` takes both outputs from a single one. Since
 *   the intermediates are indistinguishable modulo, these two must have
 *   the same hash modulo.
 */
class HashModuloTest : public LibStoreTest, public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "hash-modulo";

public:
    /**
     * The stems `named` knows about, each of which gets a `.json` and a
     * `.drv` golden master.
     */
    static constexpr std::string_view stems[] = {
        "source-first",
        "source-second",
        "intermediate-first",
        "intermediate-second",
        "parent-split",
        "parent-joined",
    };

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

protected:
    HashModuloTest()
        : LibStoreTest([] {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            return config->openDummyStore();
        }())
    {
    }

    /**
     * A fixed-output derivation, whose output path (and therefore whose
     * hash modulo) does not depend on `builder`.
     */
    Full makeSource(std::string_view builder)
    {
        Full drv{
            .outputs{
                {
                    "out",
                    Output::CAFixed{
                        .ca{
                            .method = ContentAddressMethod::Raw::NixArchive,
                            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
                        },
                    },
                },
            },
            .platform = "x86_64-linux",
            .builder = std::string{builder},
            .name = "source",
        };
        fillInOutputPaths(drv, *store);
        return drv;
    }

    /**
     * A regular (input-addressed) derivation with two outputs, taking
     * one of the sources as its only input derivation.
     */
    Full makeIntermediate(const Full & source, std::string_view builder = "/bin/intermediate")
    {
        Full drv{
            .outputs{
                {"dev", Output::Deferred{}},
                {"out", Output::Deferred{}},
            },
            .inputs{
                SingleDerivedPath::Built{
                    .drvPath = drvRef(source),
                    .output = "out",
                },
            },
            .platform = "x86_64-linux",
            .builder = std::string{builder},
            .name = "intermediate",
        };
        fillInOutputPaths(drv, *store);
        return drv;
    }

    /**
     * A derivation depending on the given outputs of the given input
     * derivations, with its own output left deferred so that
     * `hash` masks it.
     */
    Full makeParent(std::set<SingleDerivedPath> inputs)
    {
        return Full{
            .outputs = {{"out", Output::Deferred{}}},
            .inputs = std::move(inputs),
            .platform = "x86_64-linux",
            .builder = "/bin/parent",
            .name = "parent",
        };
    }

    ref<const SingleDerivedPath> drvRef(const Full & drv)
    {
        return makeConstantStorePathRef(store->writeDerivation(drv, NoRepair));
    }

    /**
     * The derivation each golden master stem stands for. Built on
     * demand so that each test gets its own store.
     */
    Full named(std::string_view stem)
    {
        if (stem == "source-first")
            return makeSource("/bin/first");
        if (stem == "source-second")
            return makeSource("/bin/second");
        if (stem == "intermediate-first")
            return makeIntermediate(makeSource("/bin/first"));
        if (stem == "intermediate-second")
            return makeIntermediate(makeSource("/bin/second"));
        if (stem == "parent-split")
            return makeParent({
                SingleDerivedPath::Built{.drvPath = drvRef(named("intermediate-first")), .output = "out"},
                SingleDerivedPath::Built{.drvPath = drvRef(named("intermediate-second")), .output = "dev"},
            });
        if (stem == "parent-joined")
            return makeParent({
                SingleDerivedPath::Built{.drvPath = drvRef(named("intermediate-second")), .output = "out"},
                SingleDerivedPath::Built{.drvPath = drvRef(named("intermediate-second")), .output = "dev"},
            });
        ADD_FAILURE() << "no such derivation '" << stem << "'";
        return {};
    }
};

struct HashModuloJsonTest : HashModuloTest,
                            JsonCharacterizationTest<Full>,
                            ::testing::WithParamInterface<std::string_view>
{};

TEST_P(HashModuloJsonTest, from_json)
{
    readJsonTest(GetParam(), named(GetParam()));
}

TEST_P(HashModuloJsonTest, to_json)
{
    writeJsonTest(GetParam(), named(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(HashModuloJSON, HashModuloJsonTest, ::testing::ValuesIn(HashModuloTest::stems));

struct HashModuloATermTest : HashModuloTest, ::testing::WithParamInterface<std::string_view>
{};

TEST_P(HashModuloATermTest, parse)
{
    auto expected = named(GetParam());
    readTest(std::string{GetParam()} + ".drv", [&](auto encoded) {
        auto parsed = parse(*store, std::move(encoded), expected.name);
        EXPECT_EQ(parsed, expected);
    });
}

TEST_P(HashModuloATermTest, unparse)
{
    writeTest(std::string{GetParam()} + ".drv", [&] { return unparse(named(GetParam()), *store); });
}

INSTANTIATE_TEST_SUITE_P(HashModuloATerm, HashModuloATermTest, ::testing::ValuesIn(HashModuloTest::stems));

/**
 * The intermediate ATerm the input address is computed from. It is not
 * a derivation --- input derivations appear as bare hashes rather than
 * store paths --- so it is characterized in one direction only.
 *
 * The fixed-output `source-*` derivations have no single hash modulo,
 * and so no intermediate ATerm either; they are not included.
 */
struct HashModuloModuloTest : HashModuloTest, ::testing::WithParamInterface<std::string_view>
{};

TEST_P(HashModuloModuloTest, unparse)
{
    writeTest(std::string{GetParam()} + "-modulo.drv", [&] {
        auto encoded = unparseModulo(*store, named(GetParam()));
        EXPECT_TRUE(encoded);
        return encoded.value_or("");
    });
}

INSTANTIATE_TEST_SUITE_P(
    HashModuloModulo,
    HashModuloModuloTest,
    ::testing::Values("intermediate-first", "intermediate-second", "parent-split", "parent-joined"));

/**
 * `intermediate-first` and `intermediate-second` are distinct
 * derivations that are nonetheless identical modulo their fixed-output
 * inputs, which is what makes `parent-split` interesting: its two input
 * derivations collide on a single key in the intermediate `inputDrvs`
 * map. The output names requested of each must be *merged*, not
 * clobbered.
 */
TEST_F(HashModuloTest, collidingInputDrvsMergeOutputNames)
{
    auto first = named("intermediate-first");
    auto second = named("intermediate-second");

    /* Same output paths (that is the point), different derivations. */
    EXPECT_EQ(first.outputs, second.outputs);
    EXPECT_NE(store->writeDerivation(first, NoRepair), store->writeDerivation(second, NoRepair));

    /* ...and so the same intermediate ATerm, hence the same hash. */
    EXPECT_EQ(unparseModulo(*store, first), unparseModulo(*store, second));

    /* `out` from one and `dev` from the other therefore hashes the same
       as both outputs from a single one of them. */
    EXPECT_EQ(hash(*store, named("parent-split")), hash(*store, named("parent-joined")));

    /* ...and symmetrically, with the roles of the two swapped. */
    auto splitOther = makeParent({
        SingleDerivedPath::Built{.drvPath = drvRef(first), .output = "dev"},
        SingleDerivedPath::Built{.drvPath = drvRef(second), .output = "out"},
    });
    EXPECT_EQ(hash(*store, splitOther), hash(*store, named("parent-joined")));
}

/**
 * Which outputs are requested is part of the hash: asking for `out`
 * alone is not the same as asking for `out` and `dev`.
 */
TEST_F(HashModuloTest, differingOutputNamesDiffer)
{
    auto intermediate = drvRef(named("intermediate-first"));

    auto justOut = makeParent({
        SingleDerivedPath::Built{.drvPath = intermediate, .output = "out"},
    });
    auto both = makeParent({
        SingleDerivedPath::Built{.drvPath = intermediate, .output = "out"},
        SingleDerivedPath::Built{.drvPath = intermediate, .output = "dev"},
    });

    EXPECT_NE(hash(*store, justOut), hash(*store, both));
}

/**
 * A fixed-output derivation's hash modulo is a map of output hashes,
 * and does not depend on how the output is produced. This is the
 * premise the collision above rests on.
 */
TEST_F(HashModuloTest, fixedOutputIsProvenanceFree)
{
    auto first = hashInput(*store, named("source-first"));
    auto second = hashInput(*store, named("source-second"));

    auto * outputHashes = std::get_if<HashModulo::CaOutputHashes>(&first.raw);
    ASSERT_TRUE(outputHashes);
    EXPECT_EQ(outputHashes->size(), 1u);
    EXPECT_EQ(first, second);
}

/**
 * Derivations that differ in a way which is *not* modded out have
 * different hashes modulo, and so their dependents do too.
 */
TEST_F(HashModuloTest, differingInputDrvsDiffer)
{
    auto source = makeSource("/bin/first");

    auto a = makeParent({
        SingleDerivedPath::Built{.drvPath = drvRef(makeIntermediate(source)), .output = "out"},
    });
    auto b = makeParent({
        SingleDerivedPath::Built{
            .drvPath = drvRef(makeIntermediate(source, "/bin/something-else")),
            .output = "out",
        },
    });

    EXPECT_NE(hash(*store, a), hash(*store, b));
}

} // namespace nix::derivation::modulo

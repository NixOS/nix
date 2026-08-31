#include <gtest/gtest.h>

#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/masked.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/util/tests/characterization.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix::derivation::masked {

/**
 * Tests for `fullyMaskDerivation`, `hashDerivation`, and
 * `hashInput`.
 *
 * The point of "hash modulo" is that derivations which differ only in
 * the *provenance* of their fixed-output inputs are indistinguishable:
 * two `fetchurl` calls with the same `outputHash` but different URLs
 * have different derivation paths, but everything downstream of them
 * should have the same output paths.
 *
 * Besides the derivations themselves, we characterize the fully masked
 * derivation --- the thing that is actually hashed --- both structurally
 * and as an ATerm golden master, so that the input-addressing
 * computation is reviewable, and not just a hash we would have to take
 * on faith.
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
class HashModuloTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "masked";

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

private:
    std::string storeDir{"/nix/store"};
protected:
    /**
     * `StoreDirConfig` holds the store directory *by reference*, so the string
     * has to outlive it; declared first so it is initialised first.
     */
    StoreDirConfig store{storeDir};

    /**
     * The input derivations this fixture has handed out references to.
     *
     * The library asks for a `ReadDerivation` rather than a whole
     * store, so the test can be its own: no store is opened, nothing is
     * written, and what the recursion sees is plainly whatever was put
     * here.
     */
    using Written = std::map<StorePath, Full>;

    static ReadDerivation readDrv(Written & written)
    {
        return [&written](const StorePath & drvPath) { return written.at(drvPath); };
    }

    /**
     * A fixed-output derivation, whose output path (and therefore whose
     * hash modulo) does not depend on `builder`.
     */
    Full makeSource(Written & written, std::string_view builder)
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
        fillInOutputPaths(drv, store, readDrv(written));
        return drv;
    }

    /**
     * A regular (input-addressed) derivation with two outputs, taking
     * one of the sources as its only input derivation.
     */
    FullInputAddressed
    makeIntermediate(Written & written, const Full & source, std::string_view builder = "/bin/intermediate")
    {
        FullDeferred drv{
            .outputs{
                {"dev", {}},
                {"out", {}},
            },
            .inputs{
                SingleDerivedPath::Built{
                    .drvPath = drvRef(written, source),
                    .output = "out",
                },
            },
            .platform = "x86_64-linux",
            .builder = std::string{builder},
            .name = "intermediate",
        };
        auto filledIn = fillInOutputPaths(std::move(drv), store, readDrv(written));
        EXPECT_TRUE(filledIn);
        return filledIn.value_or(FullInputAddressed{});
    }

    /**
     * A derivation depending on the given outputs of the given input
     * derivations, with its own output left deferred so that
     * `fullyMaskDerivation` masks it.
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

    /**
     * The `Full`-taking helpers below want the `Output` variant, so a
     * statically input-addressed derivation has to widen back to it.
     */
    static Full widen(const FullInputAddressed & drv)
    {
        return drv.mapOutputs([](const Output::InputAddressed & o) -> Output { return o; });
    }

    ref<const SingleDerivedPath> drvRef(Written & written, const Full & drv)
    {
        auto drvPath = computeStorePath(store, drv);
        written.insert_or_assign(drvPath, drv);
        return makeConstantStorePathRef(std::move(drvPath));
    }

    ref<const SingleDerivedPath> drvRef(Written & written, const FullInputAddressed & drv)
    {
        return drvRef(written, widen(drv));
    }

    /**
     * Every caller wants the same store directory and the same lookup,
     * so that convenience belongs here rather than in the library.
     */
    std::optional<Drv<Output::Deferred>> bothMasked(Written & written, const Full & drv)
    {
        return fullyMaskDerivation(store, readDrv(written), drv);
    }

    HashModulo inputModulo(Written & written, const Full & drv)
    {
        return hashInput(store, readDrv(written), drv);
    }

    /**
     * The derivation each golden master stem stands for. Built on
     * demand so that each test gets its own store.
     */
    Full named(Written & written, std::string_view stem)
    {
        if (stem == "source-first")
            return makeSource(written, "/bin/first");
        if (stem == "source-second")
            return makeSource(written, "/bin/second");
        if (stem == "intermediate-first")
            return widen(makeIntermediate(written, makeSource(written, "/bin/first")));
        if (stem == "intermediate-second")
            return widen(makeIntermediate(written, makeSource(written, "/bin/second")));
        if (stem == "parent-split")
            return makeParent({
                SingleDerivedPath::Built{
                    .drvPath = drvRef(written, named(written, "intermediate-first")), .output = "out"},
                SingleDerivedPath::Built{
                    .drvPath = drvRef(written, named(written, "intermediate-second")), .output = "dev"},
            });
        if (stem == "parent-joined")
            return makeParent({
                SingleDerivedPath::Built{
                    .drvPath = drvRef(written, named(written, "intermediate-second")), .output = "out"},
                SingleDerivedPath::Built{
                    .drvPath = drvRef(written, named(written, "intermediate-second")), .output = "dev"},
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
    Written written;
    readJsonTest(GetParam(), named(written, GetParam()));
}

TEST_P(HashModuloJsonTest, to_json)
{
    Written written;
    writeJsonTest(GetParam(), named(written, GetParam()));
}

INSTANTIATE_TEST_SUITE_P(HashModuloJSON, HashModuloJsonTest, ::testing::ValuesIn(HashModuloTest::stems));

struct HashModuloATermTest : HashModuloTest, ::testing::WithParamInterface<std::string_view>
{};

TEST_P(HashModuloATermTest, parse)
{
    Written written;
    auto expected = named(written, GetParam());
    readTest(std::string{GetParam()} + ".drv", [&](auto encoded) {
        auto parsed = parse(store, std::move(encoded), expected.name);
        EXPECT_EQ(parsed, expected);
    });
}

TEST_P(HashModuloATermTest, unparse)
{
    Written written;
    writeTest(std::string{GetParam()} + ".drv", [&] { return unparse(named(written, GetParam()), store); });
}

INSTANTIATE_TEST_SUITE_P(HashModuloATerm, HashModuloATermTest, ::testing::ValuesIn(HashModuloTest::stems));

/**
 * The fully masked derivation that the input address is computed
 * from. It is not
 * a derivation that can be built or written --- its input derivations
 * are named by hash rather than by store path --- so it is characterized
 * in one direction only.
 *
 * The fixed-output `source-*` derivations have no single hash modulo,
 * and so no masked form either; they are not included.
 */
struct HashModuloFullyMaskedTest : HashModuloTest, ::testing::WithParamInterface<std::string_view>
{};

TEST_P(HashModuloFullyMaskedTest, unparse)
{
    Written written;
    writeTest(std::string{GetParam()} + "-fully-masked.drv", [&] {
        auto m = bothMasked(written, named(written, GetParam()));
        EXPECT_TRUE(m);
        return m ? unparse(*m, store) : "";
    });
}

INSTANTIATE_TEST_SUITE_P(
    HashModuloFullyMasked,
    HashModuloFullyMaskedTest,
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
    Written written;
    auto first = named(written, "intermediate-first");
    auto second = named(written, "intermediate-second");

    /* Same output paths (that is the point), different derivations. */
    EXPECT_EQ(first.outputs, second.outputs);
    EXPECT_NE(computeStorePath(store, first), computeStorePath(store, second));

    /* ...and so the same masked derivation, hence the same hash. */
    EXPECT_EQ(bothMasked(written, first), bothMasked(written, second));

    /* `out` from one and `dev` from the other therefore gives the same
       masked derivation as both outputs from a single one of them:
       one merged `inputDrvs` entry naming `dev` and `out`. Comparing
       the structure rather than the hash means a regression here says
       which output names went missing. */
    EXPECT_EQ(
        bothMasked(written, named(written, "parent-split")), bothMasked(written, named(written, "parent-joined")));

    /* ...and symmetrically, with the roles of the two swapped. */
    auto splitOther = makeParent({
        SingleDerivedPath::Built{.drvPath = drvRef(written, first), .output = "dev"},
        SingleDerivedPath::Built{.drvPath = drvRef(written, second), .output = "out"},
    });
    EXPECT_EQ(bothMasked(written, splitOther), bothMasked(written, named(written, "parent-joined")));
}

/**
 * Which outputs are requested is part of the hash: asking for `out`
 * alone is not the same as asking for `out` and `dev`.
 */
TEST_F(HashModuloTest, differingOutputNamesDiffer)
{
    Written written;
    auto intermediate = drvRef(written, named(written, "intermediate-first"));

    auto justOut = makeParent({
        SingleDerivedPath::Built{.drvPath = intermediate, .output = "out"},
    });
    auto both = makeParent({
        SingleDerivedPath::Built{.drvPath = intermediate, .output = "out"},
        SingleDerivedPath::Built{.drvPath = intermediate, .output = "dev"},
    });

    EXPECT_NE(bothMasked(written, justOut), bothMasked(written, both));
}

/**
 * A fixed-output derivation's hash modulo is a map of output hashes,
 * and does not depend on how the output is produced. This is the
 * premise the collision above rests on.
 */
TEST_F(HashModuloTest, fixedOutputIsProvenanceFree)
{
    Written written;
    auto first = inputModulo(written, named(written, "source-first"));
    auto second = inputModulo(written, named(written, "source-second"));

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
    Written written;
    auto source = makeSource(written, "/bin/first");

    auto a = makeParent({
        SingleDerivedPath::Built{.drvPath = drvRef(written, makeIntermediate(written, source)), .output = "out"},
    });
    auto b = makeParent({
        SingleDerivedPath::Built{
            .drvPath = drvRef(written, makeIntermediate(written, source, "/bin/something-else")),
            .output = "out",
        },
    });

    EXPECT_NE(bothMasked(written, a), bothMasked(written, b));
}

} // namespace nix::derivation::masked

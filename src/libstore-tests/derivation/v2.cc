#include <gtest/gtest.h>

#include "nix/store/derivation/v2/aterm.hh"
#include "nix/store/derivation/v2/convert.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/util/tests/characterization.hh"

namespace nix::derivation::v2 {

class V2Test : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "v2";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

protected:
    std::string storeDir{"/nix/store"};
    StoreDirConfig store{storeDir};
    ExperimentalFeatureSettings xp;

    StorePath dep{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep.drv"};
    StorePath src{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-src"};

    void SetUp() override
    {
        xp.set("experimental-features", "ca-derivations impure-derivations dynamic-derivations");
    }

    SingleDerivedPath depOut() const
    {
        return SingleDerivedPath::Built{
            .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{dep}),
            .output = "out",
        };
    }

    Full fullDerivation() const
    {
        Full drv{
            .name = "hello",
            .inputs = FullInputs::fromSet({depOut(), SingleDerivedPath::Opaque{src}}),
            .platform = "x86_64-linux",
        };
        drv.builder.append(Reference<SingleDerivedPath>{SingleDerivedPath::Opaque{src}});
        drv.builder.append(std::string{"/bin/sh"});
        Interpolated<SingleDerivedPath> arg;
        arg.append(std::string{"echo \"hi\" > "});
        arg.append(Reference<SingleDerivedPath>{OutputName{"out"}});
        drv.args.push_back(std::move(arg));
        Interpolated<SingleDerivedPath> path;
        path.append(Reference<SingleDerivedPath>{depOut()});
        path.append(std::string{"/bin"});
        drv.env.emplace("PATH", EnvVar<SingleDerivedPath>{.value = std::move(path)});
        drv.env.emplace("big", EnvVar<SingleDerivedPath>{.value = Interpolated<SingleDerivedPath>{"x"}, .passAsFile = true});
        drv.options.preferLocalBuild = true;
        drv.options.requiredSystemFeatures = {"kvm", "big-parallel"};
        drv.options.exportReferencesGraph.emplace("graph", std::set<SingleDerivedPath>{depOut()});
        drv.options.unknown.emplace("lix:frobnicate", Term{Term::List{{Term{"a"}, Term{Term::Tuple{{Term{"b"}}}}}}});
        drv.outputs.emplace(
            "out",
            Output<SingleDerivedPath>{
                .raw = derivation::Output::CAFloating{.method = ContentAddressMethod::Raw::NixArchive, .hashAlgo = HashAlgorithm::SHA256},
                .options =
                    {
                        .checks =
                            {
                                .maxSize = 1024,
                                .allowedReferences = std::set<Reference<SingleDerivedPath>>{},
                                .disallowedReferences =
                                    {Reference<SingleDerivedPath>{depOut()}, Reference<SingleDerivedPath>{OutputName{"dev"}}},
                            },
                        .unsafeDiscardReferences = true,
                    },
            });
        drv.outputs.emplace(
            "dev",
            Output<SingleDerivedPath>{
                .raw = derivation::Output::CAFloating{.method = ContentAddressMethod::Raw::NixArchive, .hashAlgo = HashAlgorithm::SHA256},
            });
        return drv;
    }

    void expectRejected(std::string s, std::string_view what)
    {
        SCOPED_TRACE(what);
        EXPECT_THROW(parse<SingleDerivedPath>(store, s, "hello", xp), Error) << s;
    }
};

TEST_F(V2Test, roundTripFull)
{
    auto drv = fullDerivation();
    auto s = unparse(drv, store, {});
    auto parsed = parse<SingleDerivedPath>(store, s, drv.name, xp);
    EXPECT_EQ(parsed.drv, drv);
    EXPECT_TRUE(parsed.recordedOutputPaths.empty());
    EXPECT_EQ(unparse(parsed.drv, store, {}), s);
}

TEST_F(V2Test, fullGolden)
{
    auto drv = fullDerivation();
    writeTest("full.drv", [&] { return unparse(drv, store, {}); });
    readTest("full.drv", [&](std::string encoded) {
        EXPECT_EQ(parse<SingleDerivedPath>(store, encoded, drv.name, xp).drv, drv);
    });
}

TEST_F(V2Test, roundTripBasic)
{
    Basic drv{
        .name = "hello",
        .inputs = {src},
        .platform = "x86_64-linux",
        .builder = Interpolated<StorePath>{store.printStorePath(src) + "/bin/sh"},
    };
    Interpolated<StorePath> out;
    out.append(OutputReference{"out"});
    drv.env.emplace("out", EnvVar<StorePath>{.value = std::move(out)});
    drv.outputs.emplace("out", Output<StorePath>{.raw = InputAddressed{}});

    auto paths = outputPaths(store, drv);
    ASSERT_EQ(paths.size(), 1u);
    auto s = unparse(drv, store, paths);
    auto parsed = parse<StorePath>(store, s, drv.name, xp);
    EXPECT_EQ(parsed.drv, drv);
    EXPECT_EQ(parsed.recordedOutputPaths, paths);
}

TEST_F(V2Test, interfaceBasic)
{
    Basic data{
        .name = "hello",
        .inputs = {src},
        .platform = "x86_64-linux",
        .builder = Interpolated<StorePath>{store.printStorePath(src) + "/bin/sh"},
    };
    Interpolated<StorePath> out;
    out.append(std::string{"to "});
    out.append(OutputReference{"out"});
    data.env.emplace("out", EnvVar<StorePath>{.value = std::move(out), .passAsFile = true});
    data.outputs.emplace("out", Output<StorePath>{.raw = InputAddressed{}});
    auto paths = outputPaths(store, data);

    Derivation<StorePath> drv{data, paths};
    const BasicInterface & i = drv;

    EXPECT_EQ(i.name(), "hello");
    EXPECT_EQ(i.inputs(), StorePathSet{src});
    EXPECT_EQ(
        i.outputs().at("out").output,
        derivation::Output{derivation::Output::InputAddressed{.path = paths.at("out")}});
    EXPECT_FALSE(i.isBuiltin());

    auto render = [](const OutputName & name) { return "<" + name + ">"; };
    EXPECT_EQ(i.builder(render), store.printStorePath(src) + "/bin/sh");
    EXPECT_EQ(
        i.env(render).at("out"),
        (RenderedEnvVar{
            .value = "to <out>",
            .passAsFile = true,
        }));
}

TEST_F(V2Test, interfaceFullDeferred)
{
    /* Without the output paths, as when some input's are not static,
       the input-addressed outputs are deferred. */
    auto full = fullDerivation();
    full.outputs.at("out").raw = InputAddressed{};
    full.outputs.erase("dev");
    Derivation<SingleDerivedPath> drv{full, {}};
    const FullInterface & i = drv;

    EXPECT_EQ(i.outputs().at("out").output, derivation::Output{derivation::Output::Deferred{}});
    EXPECT_EQ(i.outputs().at("out").options.unsafeDiscardReferences, true);
    EXPECT_EQ(i.getRequiredSystemFeatures(), (StringSet{"big-parallel", "ca-derivations", "kvm"}));
}

TEST_F(V2Test, staticSpeculation)
{
    /* With every input static, the output paths are those of the
       resolved derivation. */
    Full drv{
        .name = "hello",
        .inputs = FullInputs::fromSet({depOut()}),
        .platform = "x86_64-linux",
    };
    drv.builder.append(Reference<SingleDerivedPath>{depOut()});
    drv.outputs.emplace("out", Output<SingleDerivedPath>{.raw = InputAddressed{}});

    StorePath depOutPath{"8j9j9j9j9j9j9j9j9j9j9j9j9j9j9j9j-dep"};
    auto known = [&](const SingleDerivedPath::Built &) -> std::optional<StorePath> { return depOutPath; };
    auto unknown = [&](const SingleDerivedPath::Built &) -> std::optional<StorePath> { return std::nullopt; };

    auto paths = staticOutputPaths(store, drv, known);
    ASSERT_TRUE(paths);
    auto resolved = resolve(store, drv, known);
    ASSERT_TRUE(resolved);
    EXPECT_EQ(*paths, outputPaths(store, *resolved));
    EXPECT_EQ(resolved->builder, Interpolated<StorePath>{store.printStorePath(depOutPath)});

    EXPECT_FALSE(staticOutputPaths(store, drv, unknown));
}

TEST_F(V2Test, rejectsNonCanonical)
{
    auto good = unparse(fullDerivation(), store, {});
    auto replace = [&](std::string_view from, std::string_view to) {
        auto i = good.find(from);
        EXPECT_NE(i, std::string::npos) << from;
        return good.substr(0, i) + std::string{to} + good.substr(i + from.size());
    };

    expectRejected(replace("\"nix:preferLocalBuild\",\"true\"", "\"nix:preferLocalBuild\",\"false\""),
                   "redundant default");
    expectRejected(replace("(\"nix:preferLocalBuild\",\"true\")", "(\"preferLocalBuild\",\"true\")"),
                   "option name without namespace");
    expectRejected(replace("[\"echo \\\"hi\\\" > \",", "[\"echo \",\"\\\"hi\\\" > \","), "adjacent literals");
    expectRejected(replace("[\"echo \\\"hi\\\" > \",", "[\"\",\"echo \\\"hi\\\" > \","), "empty literal");
    expectRejected(replace("(\"output\",\"out\")", "(\"output\",\"nope\")"), "reference to a missing output");
    expectRejected(replace("\"r:sha256\",\"\"", "\"\",\"abc\""), "hash without hash algorithm");
    expectRejected(replace("\"r:sha256\",\"\"", "\"r:sha256\",\"abc\""), "fixed output");
}

TEST_F(V2Test, basicRejectsInputReferences)
{
    auto s = unparse(fullDerivation(), store, {});
    EXPECT_THROW(parse<StorePath>(store, s, "hello", xp), Error);
}

} // namespace nix::derivation::v2

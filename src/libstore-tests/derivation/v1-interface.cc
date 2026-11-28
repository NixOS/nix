#include <gtest/gtest.h>

#include "nix/store/derivation/v1.hh"
#include "nix/store/derivation/v1/traditional.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/util/file-system.hh"
#include "nix/util/tests/characterization.hh"

namespace nix::derivation::v1 {

/**
 * A version 1 derivation's getters agree with its elaborated form, so
 * that whatever reads the one can read the other instead.
 */
class V1InterfaceTest : public CharacterizationTest, public ::testing::WithParamInterface<std::string>
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "ia";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

protected:
    std::string storeDir{"/nix/store"};
    StoreDirConfig store{storeDir};
};

TEST_P(V1InterfaceTest, agreesWithElaborated)
{
    auto & name = GetParam();
    auto parsed = TraditionalParsed::parse(store, readFile(goldenMaster(name + ".drv")), name);
    auto drv = fromParsed(parsed, name, store);
    auto old = elaborated(drv);

    std::visit(
        [&](const FullInterface & i) {
            EXPECT_EQ(i.name(), old.name);
            EXPECT_EQ(i.platform(), old.platform);
            EXPECT_EQ(i.inputs(), old.inputs);
            EXPECT_EQ(i.outputNames(), old.outputNames());
            EXPECT_EQ(i.getRequiredSystemFeatures(), old.getRequiredSystemFeatures());
            EXPECT_EQ(i.isBuiltin(), old.isBuiltin());

            /* Rendering each output as its placeholder gives back the
               strings as they are in the elaborated form. */
            auto placeholder = [](const DrvRef<SingleDerivedPath> & ref) {
                return hashPlaceholder(std::get<OutputName>(ref));
            };
            EXPECT_EQ(i.builder(placeholder), old.builder);
            EXPECT_EQ(i.args(placeholder), old.args);
            auto env = i.env(placeholder);
            EXPECT_EQ(env.size(), old.env.size());
            for (auto & [varName, var] : old.env) {
                ASSERT_TRUE(env.contains(varName)) << varName;
                EXPECT_EQ(env.at(varName).value, var.value) << varName;
                EXPECT_EQ(env.at(varName).passAsFile, var.passAsFile) << varName;
            }

            EXPECT_EQ(i.options(), static_cast<const CommonTopOptions<SingleDerivedPath> &>(old.options));
            for (auto & [outputName, output] : i.outputs()) {
                auto & oldOutput = old.outputs.at(outputName);
                EXPECT_EQ(output.output, oldOutput.output) << outputName;
                /* Checks given once for all outputs are each output's. */
                auto checks = oldOutput.options.checks ? oldOutput.options.checks : old.options.allOutputChecks;
                EXPECT_EQ(output.options.checks, checks) << outputName;
                EXPECT_EQ(output.options.unsafeDiscardReferences, oldOutput.options.unsafeDiscardReferences)
                    << outputName;
            }
        },
        drv);
}

INSTANTIATE_TEST_SUITE_P(
    Goldens,
    V1InterfaceTest,
    ::testing::Values(
        "advanced-attributes-defaults",
        "advanced-attributes",
        "advanced-attributes-structured-attrs-defaults",
        "advanced-attributes-structured-attrs"),
    [](const auto & info) {
        auto s = info.param;
        std::replace(s.begin(), s.end(), '-', '_');
        return s;
    });

} // namespace nix::derivation::v1

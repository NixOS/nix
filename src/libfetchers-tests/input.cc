#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/attrs.hh"
#include "nix/fetchers/fetchers.hh"

#include <gtest/gtest.h>

#include <string>

namespace nix {

using fetchers::Attr;

struct InputFromAttrsTestCase
{
    fetchers::Attrs attrs;
    std::string expectedUrl;
    std::string description;
    fetchers::Attrs expectedAttrs = attrs;
};

class InputFromAttrsTest : public ::testing::WithParamInterface<InputFromAttrsTestCase>, public ::testing::Test
{};

TEST_P(InputFromAttrsTest, attrsAreCorrectAndRoundTrips)
{
    fetchers::Settings fetchSettings;

    const auto & testCase = GetParam();

    auto input = fetchers::Input::fromAttrs(fetchSettings, fetchers::Attrs(testCase.attrs));

    EXPECT_EQ(input.toAttrs(), testCase.expectedAttrs);
    EXPECT_EQ(input.toURLString(), testCase.expectedUrl);

    auto input2 = fetchers::Input::fromAttrs(fetchSettings, input.toAttrs());
    EXPECT_EQ(input, input2);
    EXPECT_EQ(input.toAttrs(), input2.toAttrs());
}

INSTANTIATE_TEST_SUITE_P(
    InputFromAttrs,
    InputFromAttrsTest,
    ::testing::Values(
        // Test for issue #14429.
        InputFromAttrsTestCase{
            .attrs =
                {
                    {"url", Attr("git+ssh://git@github.com/NixOS/nixpkgs")},
                    {"type", Attr("git")},
                },
            .expectedUrl = "git+ssh://git@github.com/NixOS/nixpkgs",
            .description = "strips_git_plus_prefix",
            .expectedAttrs =
                {
                    {"url", Attr("ssh://git@github.com/NixOS/nixpkgs")},
                    {"type", Attr("git")},
                },
        }),
    [](const ::testing::TestParamInfo<InputFromAttrsTestCase> & info) { return info.param.description; });

} // namespace nix

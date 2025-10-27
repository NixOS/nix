#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

#include <kaitai/kaitaistream.h>

#include <fstream>
#include <string>
#include <vector>

#include "nix_nar.h"

static const std::vector<std::string> NarFiles = {
    "empty.nar",
    "dot.nar",
    "dotdot.nar",
    "executable-after-contents.nar",
    "invalid-tag-instead-of-contents.nar",
    "name-after-node.nar",
    "nul-character.nar",
    "slash.nar",
};

class NarParseTest : public ::testing::TestWithParam<std::string>
{};

TEST_P(NarParseTest, ParseSucceeds)
{
    const auto nar_file = GetParam();

    const char * nars_dir_env = std::getenv("NIX_NARS_DIR");
    if (nars_dir_env == nullptr) {
        FAIL() << "NIX_NARS_DIR environment variable not set.";
    }

    const std::filesystem::path nar_file_path = std::filesystem::path(nars_dir_env) / "dot.nar";
    ASSERT_TRUE(std::filesystem::exists(nar_file_path)) << "Missing test file: " << nar_file_path;

    std::ifstream ifs(nar_file_path, std::ifstream::binary);
    ASSERT_TRUE(ifs.is_open()) << "Failed to open file: " << nar_file;
    kaitai::kstream ks(&ifs);
    nix_nar_t nar(&ks);
    ASSERT_TRUE(nar.root_node() != nullptr) << "Failed to parse NAR file: " << nar_file;
}

INSTANTIATE_TEST_SUITE_P(AllNarFiles, NarParseTest, ::testing::ValuesIn(NarFiles));

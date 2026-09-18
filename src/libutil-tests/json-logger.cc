#include "nix/util/logging.hh"
#include "nix/util/file-system.hh"
#include "nix/util/strings.hh"

#include <gtest/gtest.h>

namespace nix {

static size_t countLines(const std::filesystem::path & path, std::string_view needle)
{
    size_t n = 0;
    for (auto & line : tokenizeString<Strings>(readFile(path), "\n"))
        if (line.find(needle) != line.npos)
            n++;
    return n;
}

TEST(JSONLogger, progressIsRateLimited)
{
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto path = tmpDir / "log";
    {
        auto jsonLogger = makeJSONLogger(path, false);
        Activity act(*jsonLogger, lvlInfo, actCopyPath, "copy", {}, 0);
        for (uint64_t i = 1; i <= 100000; ++i)
            act.progress(i, 100000);
    }
    EXPECT_LT(countLines(path, "\"type\":105"), 100u);
    EXPECT_EQ(countLines(path, "\"fields\":[100000,100000,0,0]"), 1u);
}

TEST(JSONLogger, otherResultsAreNotRateLimited)
{
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto path = tmpDir / "log";
    {
        auto jsonLogger = makeJSONLogger(path, false);
        Activity act(*jsonLogger, lvlInfo, actBuild, "build", {}, 0);
        for (int i = 0; i < 1000; ++i)
            act.result(resBuildLogLine, "x");
    }
    EXPECT_EQ(countLines(path, "\"type\":101"), 1000u);
}

} // namespace nix

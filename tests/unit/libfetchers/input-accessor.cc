#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <input-accessor.hh>
#include <memory-input-accessor.hh>
#include "terminal.hh"

namespace nix {

TEST(SourcePath, followSymlinks_cycle) {
    auto fs = makeMemoryInputAccessor();
    fs->addSymlink({"origin", CanonPath::root}, "a");
    fs->addSymlink({"a", CanonPath::root}, "b");
    fs->addSymlink({"b", CanonPath::root}, "a");

    ASSERT_TRUE(fs->pathExists({"a", CanonPath::root}));
    SourcePath origin { fs, CanonPath { "/origin" } };
    try {
        origin.followSymlinks();
        ASSERT_TRUE(false);
    } catch (const Error &e) {
        auto msg = filterANSIEscapes(e.what(), true);
        // EXPECT_THAT(msg, ("too many levels of symbolic links"));
        EXPECT_THAT(msg, testing::HasSubstr("too many levels of symbolic links"));
        EXPECT_THAT(msg, testing::HasSubstr("«unknown»/origin'"));
        EXPECT_THAT(msg, testing::HasSubstr("assuming it leads to a cycle after following 1000 indirections"));
    }
}

}

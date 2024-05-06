#include <gtest/gtest.h>

#include "file-system.hh"
#include "flake/flakeref.hh"

namespace nix {

/* ----------- tests for flake/flakeref.hh --------------------------------------------------*/

    /* ----------------------------------------------------------------------------
     * to_string
     * --------------------------------------------------------------------------*/

    TEST(flakeRef, to_string_doesntReencodeUrl) {
        auto s = "http://localhost:8181/test/+3d.tar.gz";
        auto flakeref = parseFlakeRef(s);
        auto parsed = flakeref.to_string();
        auto expected = "http://localhost:8181/test/%2B3d.tar.gz";

        ASSERT_EQ(parsed, expected);
    }

    TEST(flakeRef, simplePath) {
        auto raw_ref = "/foo/bar";

        auto flakeref = parseFlakeRef(raw_ref);
        auto flakeref_attrs = flakeref.toAttrs();
        ASSERT_EQ(flakeref.input.getType(), "path");
        ASSERT_EQ(fetchers::getStrAttr(flakeref_attrs, "path"), "/foo/bar");
        ASSERT_EQ(fetchers::maybeGetIntAttr(flakeref_attrs, "lastModified"), std::nullopt);
    }

    TEST(flakeRef, pathWithQuery) {
        auto raw_ref = "/foo/bar?lastModified=5";

        auto flakeref = parseFlakeRef(raw_ref);
        auto flakeref_attrs = flakeref.toAttrs();
        EXPECT_EQ(flakeref.input.getType(), "path");
        EXPECT_EQ(fetchers::getStrAttr(flakeref_attrs, "path"), "/foo/bar");
        EXPECT_EQ(fetchers::getIntAttr(flakeref_attrs, "lastModified"), 5);
    }

    TEST(flakeRef, pathWithQueryAndEmptyFragment) {
        auto raw_ref = "/foo/bar?lastModified=5#";

        auto flakeref = parseFlakeRef(raw_ref);
        auto flakeref_attrs = flakeref.toAttrs();
        EXPECT_EQ(flakeref.input.getType(), "path");
        EXPECT_EQ(fetchers::getStrAttr(flakeref_attrs, "path"), "/foo/bar");
        EXPECT_EQ(fetchers::getIntAttr(flakeref_attrs, "lastModified"), 5);
    }

    TEST(flakeRef, pathWithFragment) {
        auto raw_ref = "/foo/bar?lastModified=5#foo";

        ASSERT_THROW(
            parseFlakeRef(raw_ref),
            FlakeRefError
        );
    }

    TEST(flakeRef, relativePath) {
        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir);

        // Relative path flakerefs require a `flake.nix`
        writeFile(tmpDir + "/flake.nix", "");
        createDirs(tmpDir + "/foo");

        std::vector<std::string> raw_refs = {
            ".?lastModified=5",
            "./foo?lastModified=5",
            "./foo?lastModified=5#",
            tmpDir + "?lastModified=5",
            fmt("../%s/?lastModified=5", baseNameOf(tmpDir)),
            "./foo/..?lastModified=5"
        };

        for (auto raw_ref : raw_refs) {
            auto flakeref = parseFlakeRef(raw_ref, tmpDir);
            auto flakeref_attrs = flakeref.toAttrs();
            EXPECT_EQ(flakeref.input.getType(), "path");
            EXPECT_EQ(fetchers::getStrAttr(flakeref_attrs, "path"), tmpDir);
            EXPECT_EQ(fetchers::getIntAttr(flakeref_attrs, "lastModified"), 5);
        }
    }

}

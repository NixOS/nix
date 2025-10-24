#include <string_view>

#include "nix/util/bytes.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

namespace memory_source_accessor {

using File = MemorySourceAccessor::File;

ref<MemorySourceAccessor> exampleSimple()
{
    auto sc = make_ref<MemorySourceAccessor>();
    sc->root = File{File::Regular{
        .executable = false,
        .contents = to_owned(as_bytes("asdf")),
    }};
    return sc;
}

ref<MemorySourceAccessor> exampleComplex()
{
    auto files = make_ref<MemorySourceAccessor>();
    files->root = File::Directory{
        .contents{
            {
                "foo",
                File::Regular{
                    .contents = to_owned(as_bytes("hello\n\0\n\tworld!")),
                },
            },
            {
                "bar",
                File::Directory{
                    .contents =
                        {
                            {
                                "baz",
                                File::Regular{
                                    .executable = true,
                                    .contents = to_owned(as_bytes("good day,\n\0\n\tworld!")),
                                },
                            },
                            {
                                "quux",
                                File::Symlink{
                                    .target = "/over/there",
                                },
                            },
                        },
                },
            },
        },
    };
    return files;
}

} // namespace memory_source_accessor

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

class MemorySourceAccessorTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "memory-source-accessor";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

using nlohmann::json;

struct MemorySourceAccessorJsonTest : MemorySourceAccessorTest,
                                      JsonCharacterizationTest<MemorySourceAccessor>,
                                      ::testing::WithParamInterface<std::pair<std::string_view, MemorySourceAccessor>>
{};

TEST_P(MemorySourceAccessorJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    /* Cannot use `readJsonTest` because need to compare `root` field of
       the source accessors for equality. */
    readTest(Path{name} + ".json", [&](const auto & encodedRaw) {
        auto encoded = json::parse(encodedRaw);
        auto decoded = static_cast<MemorySourceAccessor>(encoded);
        ASSERT_EQ(decoded.root, expected.root);
    });
}

TEST_P(MemorySourceAccessorJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    MemorySourceAccessorJSON,
    MemorySourceAccessorJsonTest,
    ::testing::Values(
        std::pair{
            "simple",
            *memory_source_accessor::exampleSimple(),
        },
        std::pair{
            "complex",
            *memory_source_accessor::exampleComplex(),
        }));

} // namespace nix

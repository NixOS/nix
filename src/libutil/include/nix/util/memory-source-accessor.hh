#pragma once
///@file

#include "nix/util/source-path.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/variant-wrapper.hh"
#include "nix/util/json-impls.hh"

namespace nix {

/**
 * File System Object definitions
 *
 * @see https://nix.dev/manual/nix/latest/store/file-system-object.html
 */
namespace fso {

template<typename RegularContents>
struct Regular
{
    bool executable = false;
    RegularContents contents;

    auto operator<=>(const Regular &) const = default;
};

/**
 * Child parameter because sometimes we want "shallow" directories without
 * full file children.
 */
template<typename Child>
struct DirectoryT
{
    using Name = std::string;

    std::map<Name, Child, std::less<>> entries;

    inline bool operator==(const DirectoryT &) const noexcept;
    inline std::strong_ordering operator<=>(const DirectoryT &) const noexcept;
};

struct Symlink
{
    std::string target;

    auto operator<=>(const Symlink &) const = default;
};

/**
 * For when we know there is child, but don't know anything about it.
 *
 * This is not part of the core File System Object data model --- this
 * represents not knowing, not an additional type of file.
 */
struct Opaque
{
    auto operator<=>(const Opaque &) const = default;
};

/**
 * `File<std::string>` nicely defining what a "file system object"
 * is in Nix.
 *
 * With a different type arugment, it is also can be a "skeletal"
 * version is that abstract syntax for a "NAR listing".
 */
template<typename RegularContents, bool recur>
struct VariantT
{
    bool operator==(const VariantT &) const noexcept;
    std::strong_ordering operator<=>(const VariantT &) const noexcept;

    using Regular = nix::fso::Regular<RegularContents>;

    /**
     * In the default case, we do want full file children for our directory.
     */
    using Directory = nix::fso::DirectoryT<std::conditional_t<recur, VariantT, Opaque>>;

    using Symlink = nix::fso::Symlink;

    using Raw = std::variant<Regular, Directory, Symlink>;
    Raw raw;

    MAKE_WRAPPER_CONSTRUCTOR(VariantT);

    SourceAccessor::Stat lstat() const;
};

template<typename Child>
inline bool DirectoryT<Child>::operator==(const DirectoryT &) const noexcept = default;

template<typename Child>
inline std::strong_ordering DirectoryT<Child>::operator<=>(const DirectoryT &) const noexcept = default;

template<typename RegularContents, bool recur>
inline bool
VariantT<RegularContents, recur>::operator==(const VariantT<RegularContents, recur> &) const noexcept = default;

template<typename RegularContents, bool recur>
inline std::strong_ordering
VariantT<RegularContents, recur>::operator<=>(const VariantT<RegularContents, recur> &) const noexcept = default;

} // namespace fso

/**
 * An source accessor for an in-memory file system.
 */
struct MemorySourceAccessor : virtual SourceAccessor
{
    using File = fso::VariantT<std::string, true>;

    std::optional<File> root;

    bool operator==(const MemorySourceAccessor &) const noexcept = default;

    bool operator<(const MemorySourceAccessor & other) const noexcept
    {
        return root < other.root;
    }

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;
    using SourceAccessor::readFile;

    bool pathExists(const CanonPath & path) override;
    std::optional<Stat> maybeLstat(const CanonPath & path) override;
    DirEntries readDirectory(const CanonPath & path) override;
    std::string readLink(const CanonPath & path) override;

    /**
     * @param create If present, create this file and any parent directories
     * that are needed.
     *
     * Return null if
     *
     * - `create = false`: File does not exist.
     *
     * - `create = true`: some parent file was not a dir, so couldn't
     *   look/create inside.
     */
    File * open(const CanonPath & path, std::optional<File> create);

    SourcePath addFile(CanonPath path, std::string && contents);
};

/**
 * Write to a `MemorySourceAccessor` at the given path
 */
struct MemorySink : FileSystemObjectSink
{
    MemorySourceAccessor & dst;

    MemorySink(MemorySourceAccessor & dst)
        : dst(dst)
    {
    }

    void createDirectory(const CanonPath & path) override;

    void createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)>) override;

    void createSymlink(const CanonPath & path, const std::string & target) override;
};

template<>
struct json_avoids_null<MemorySourceAccessor::File::Regular> : std::true_type
{};

template<>
struct json_avoids_null<MemorySourceAccessor::File::Directory> : std::true_type
{};

template<>
struct json_avoids_null<MemorySourceAccessor::File::Symlink> : std::true_type
{};

template<>
struct json_avoids_null<MemorySourceAccessor::File> : std::true_type
{};

template<>
struct json_avoids_null<MemorySourceAccessor> : std::true_type
{};

} // namespace nix

namespace nlohmann {

using namespace nix;

#define ARG fso::Regular<RegularContents>
template<typename RegularContents>
JSON_IMPL_INNER(ARG);
#undef ARG

#define ARG fso::DirectoryT<Child>
template<typename Child>
JSON_IMPL_INNER(ARG);
#undef ARG

template<>
JSON_IMPL_INNER(fso::Symlink);

template<>
JSON_IMPL_INNER(fso::Opaque);

#define ARG fso::VariantT<RegularContents, recur>
template<typename RegularContents, bool recur>
JSON_IMPL_INNER(ARG);
#undef ARG

} // namespace nlohmann

JSON_IMPL(MemorySourceAccessor)

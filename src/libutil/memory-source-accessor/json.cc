#include "nix/util/memory-source-accessor.hh"
#include "nix/util/nar-accessor.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nlohmann {

// fso::Regular<RegularContents>
template<>
nix::MemorySourceAccessor::File::Regular
adl_serializer<nix::MemorySourceAccessor::File::Regular>::from_json(const json & json)
{
    using namespace nix;
    auto & obj = getObject(json);
    return {
        .executable = getBoolean(valueAt(obj, "executable")),
        .contents = getString(valueAt(obj, "contents")),
    };
}

template<>
void adl_serializer<nix::MemorySourceAccessor::File::Regular>::to_json(
    json & json, const nix::MemorySourceAccessor::File::Regular & r)
{
    json = {
        {"executable", r.executable},
        {"contents", r.contents},
    };
}

template<>
nix::NarListing::Regular adl_serializer<nix::NarListing::Regular>::from_json(const json & json)
{
    using namespace nix;
    auto & obj = getObject(json);
    auto * execPtr = optionalValueAt(obj, "executable");
    auto * sizePtr = optionalValueAt(obj, "size");
    auto * offsetPtr = optionalValueAt(obj, "narOffset");
    return NarListing::Regular{
        .executable = execPtr ? getBoolean(*execPtr) : false,
        .contents{
            .fileSize = ptrToOwned<uint64_t>(sizePtr),
            .narOffset = ptrToOwned<uint64_t>(offsetPtr).and_then(
                [](auto v) { return v != 0 ? std::optional{v} : std::nullopt; }),
        },
    };
}

template<>
void adl_serializer<nix::NarListing::Regular>::to_json(json & j, const nix::NarListing::Regular & r)
{
    if (r.contents.fileSize)
        j["size"] = *r.contents.fileSize;
    j["executable"] = r.executable;
    if (r.contents.narOffset)
        j["narOffset"] = *r.contents.narOffset;
}

template<typename Child>
void adl_serializer<nix::fso::DirectoryT<Child>>::to_json(json & j, const nix::fso::DirectoryT<Child> & d)
{
    j["entries"] = d.entries;
}

template<typename Child>
nix::fso::DirectoryT<Child> adl_serializer<nix::fso::DirectoryT<Child>>::from_json(const json & json)
{
    using namespace nix;
    auto & obj = getObject(json);
    return {
        .entries = valueAt(obj, "entries"),
    };
}

// fso::Symlink
nix::fso::Symlink adl_serializer<nix::fso::Symlink>::from_json(const json & json)
{
    using namespace nix;
    auto & obj = getObject(json);
    return {
        .target = getString(valueAt(obj, "target")),
    };
}

void adl_serializer<nix::fso::Symlink>::to_json(json & json, const nix::fso::Symlink & s)
{
    json = {
        {"target", s.target},
    };
}

// fso::Opaque
nix::fso::Opaque adl_serializer<nix::fso::Opaque>::from_json(const json &)
{
    return {};
}

void adl_serializer<nix::fso::Opaque>::to_json(json & j, const nix::fso::Opaque &)
{
    j = nlohmann::json::object();
}

// fso::VariantT<RegularContents, recur> - generic implementation
template<typename RegularContents, bool recur>
void adl_serializer<nix::fso::VariantT<RegularContents, recur>>::to_json(
    json & j, const nix::fso::VariantT<RegularContents, recur> & val)
{
    using namespace nix;
    using Variant = fso::VariantT<RegularContents, recur>;
    j = nlohmann::json::object();
    std::visit(
        overloaded{
            [&](const typename Variant::Regular & r) {
                j = r;
                j["type"] = "regular";
            },
            [&](const typename Variant::Directory & d) {
                j = d;
                j["type"] = "directory";
            },
            [&](const typename Variant::Symlink & s) {
                j = s;
                j["type"] = "symlink";
            },
        },
        val.raw);
}

template<typename RegularContents, bool recur>
nix::fso::VariantT<RegularContents, recur>
adl_serializer<nix::fso::VariantT<RegularContents, recur>>::from_json(const json & json)
{
    using namespace nix;
    using Variant = fso::VariantT<RegularContents, recur>;
    auto & obj = getObject(json);
    auto type = getString(valueAt(obj, "type"));
    if (type == "regular")
        return static_cast<typename Variant::Regular>(json);
    if (type == "directory")
        return static_cast<typename Variant::Directory>(json);
    if (type == "symlink")
        return static_cast<typename Variant::Symlink>(json);
    else
        throw Error("unknown type of file '%s'", type);
}

// Explicit instantiations for VariantT types we use
template struct adl_serializer<nix::MemorySourceAccessor::File>;
template struct adl_serializer<nix::NarListing>;
template struct adl_serializer<nix::ShallowNarListing>;

// MemorySourceAccessor
nix::MemorySourceAccessor adl_serializer<nix::MemorySourceAccessor>::from_json(const json & json)
{
    nix::MemorySourceAccessor res;
    res.root = json;
    return res;
}

void adl_serializer<nix::MemorySourceAccessor>::to_json(json & json, const nix::MemorySourceAccessor & val)
{
    json = val.root;
}

} // namespace nlohmann

#include "nix/util/memory-source-accessor.hh"
#include "nix/util/base-n.hh"
#include "nix/util/bytes.hh"
#include "nix/util/json-utils.hh"

namespace nix {

MemorySourceAccessor::File * MemorySourceAccessor::open(const CanonPath & path, std::optional<File> create)
{
    bool hasRoot = root.has_value();

    // Special handling of root directory.
    if (path.isRoot() && !hasRoot) {
        if (create) {
            root = std::move(*create);
            return &root.value();
        }
        return nullptr;
    }

    // Root does not exist.
    if (!hasRoot)
        return nullptr;

    File * cur = &root.value();

    bool newF = false;

    for (std::string_view name : path) {
        auto * curDirP = std::get_if<File::Directory>(&cur->raw);
        if (!curDirP)
            return nullptr;
        auto & curDir = *curDirP;

        auto i = curDir.contents.find(name);
        if (i == curDir.contents.end()) {
            if (!create)
                return nullptr;
            else {
                newF = true;
                i = curDir.contents.insert(
                    i,
                    {
                        std::string{name},
                        File::Directory{},
                    });
            }
        }
        cur = &i->second;
    }

    if (newF && create)
        *cur = std::move(*create);

    return cur;
}

std::string MemorySourceAccessor::readFile(const CanonPath & path)
{
    auto * f = open(path, std::nullopt);
    if (!f)
        throw Error("file '%s' does not exist", path);
    if (auto * r = std::get_if<File::Regular>(&f->raw))
        return std::string{to_str(r->contents)};
    else
        throw Error("file '%s' is not a regular file", path);
}

bool MemorySourceAccessor::pathExists(const CanonPath & path)
{
    return open(path, std::nullopt);
}

MemorySourceAccessor::Stat MemorySourceAccessor::File::lstat() const
{
    return std::visit(
        overloaded{
            [](const Regular & r) {
                return Stat{
                    .type = tRegular,
                    .fileSize = r.contents.size(),
                    .isExecutable = r.executable,
                };
            },
            [](const Directory &) {
                return Stat{
                    .type = tDirectory,
                };
            },
            [](const Symlink &) {
                return Stat{
                    .type = tSymlink,
                };
            },
        },
        this->raw);
}

std::optional<MemorySourceAccessor::Stat> MemorySourceAccessor::maybeLstat(const CanonPath & path)
{
    const auto * f = open(path, std::nullopt);
    return f ? std::optional{f->lstat()} : std::nullopt;
}

MemorySourceAccessor::DirEntries MemorySourceAccessor::readDirectory(const CanonPath & path)
{
    auto * f = open(path, std::nullopt);
    if (!f)
        throw Error("file '%s' does not exist", path);
    if (auto * d = std::get_if<File::Directory>(&f->raw)) {
        DirEntries res;
        for (auto & [name, file] : d->contents)
            res.insert_or_assign(name, file.lstat().type);
        return res;
    } else
        throw Error("file '%s' is not a directory", path);
    return {};
}

std::string MemorySourceAccessor::readLink(const CanonPath & path)
{
    auto * f = open(path, std::nullopt);
    if (!f)
        throw Error("file '%s' does not exist", path);
    if (auto * s = std::get_if<File::Symlink>(&f->raw))
        return s->target;
    else
        throw Error("file '%s' is not a symbolic link", path);
}

SourcePath MemorySourceAccessor::addFile(CanonPath path, std::vector<std::byte> && contents)
{
    // Create root directory automatically if necessary as a convenience.
    if (!root && !path.isRoot())
        open(CanonPath::root, File::Directory{});

    auto * f = open(path, File{File::Regular{}});
    if (!f)
        throw Error("file '%s' cannot be made because some parent file is not a directory", path);
    if (auto * r = std::get_if<File::Regular>(&f->raw))
        r->contents = std::move(contents);
    else
        throw Error("file '%s' is not a regular file", path);

    return SourcePath{ref(shared_from_this()), path};
}

SourcePath MemorySourceAccessor::addFile(CanonPath path, std::string_view contents)
{
    return addFile(path, to_owned(as_bytes(contents)));
}

using File = MemorySourceAccessor::File;

void MemorySink::createDirectory(const CanonPath & path)
{
    auto * f = dst.open(path, File{File::Directory{}});
    if (!f)
        throw Error("file '%s' cannot be made because some parent file is not a directory", path);

    if (!std::holds_alternative<File::Directory>(f->raw))
        throw Error("file '%s' is not a directory", path);
};

struct CreateMemoryRegularFile : CreateRegularFileSink
{
    File::Regular & regularFile;

    CreateMemoryRegularFile(File::Regular & r)
        : regularFile(r)
    {
    }

    void operator()(std::string_view data) override;
    void isExecutable() override;
    void preallocateContents(uint64_t size) override;
};

void MemorySink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
{
    auto * f = dst.open(path, File{File::Regular{}});
    if (!f)
        throw Error("file '%s' cannot be made because some parent file is not a directory", path);
    if (auto * rp = std::get_if<File::Regular>(&f->raw)) {
        CreateMemoryRegularFile crf{*rp};
        func(crf);
    } else
        throw Error("file '%s' is not a regular file", path);
}

void CreateMemoryRegularFile::isExecutable()
{
    regularFile.executable = true;
}

void CreateMemoryRegularFile::preallocateContents(uint64_t len)
{
    regularFile.contents.reserve(len);
}

void CreateMemoryRegularFile::operator()(std::string_view data_)
{
    auto data = as_bytes(data_);
    regularFile.contents.insert(regularFile.contents.end(), data.begin(), data.end());
}

void MemorySink::createSymlink(const CanonPath & path, const std::string & target)
{
    auto * f = dst.open(path, File{File::Symlink{}});
    if (!f)
        throw Error("file '%s' cannot be made because some parent file is not a directory", path);
    if (auto * s = std::get_if<File::Symlink>(&f->raw))
        s->target = target;
    else
        throw Error("file '%s' is not a symbolic link", path);
}

ref<SourceAccessor> makeEmptySourceAccessor()
{
    static auto empty = []() {
        auto empty = make_ref<MemorySourceAccessor>();
        MemorySink sink{*empty};
        sink.createDirectory(CanonPath::root);
        /* Don't forget to clear the display prefix, as the default constructed
           SourceAccessor has the «unknown» prefix. Since this accessor is supposed
           to mimic an empty root directory the prefix needs to be empty. */
        empty->setPathDisplay("");
        return empty.cast<SourceAccessor>();
    }();
    return empty;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

MemorySourceAccessor::File::Regular adl_serializer<MemorySourceAccessor::File::Regular>::from_json(const json & json)
{
    auto & obj = getObject(json);
    return MemorySourceAccessor::File::Regular{
        .executable = getBoolean(valueAt(obj, "executable")),
        .contents = to_owned(as_bytes(base64::decode(getString(valueAt(obj, "contents"))))),
    };
}

void adl_serializer<MemorySourceAccessor::File::Regular>::to_json(
    json & json, const MemorySourceAccessor::File::Regular & val)
{
    json = {
        {"executable", val.executable},
        {"contents", base64::encode(val.contents)},
    };
}

MemorySourceAccessor::File::Directory
adl_serializer<MemorySourceAccessor::File::Directory>::from_json(const json & json)
{
    auto & obj = getObject(json);
    return MemorySourceAccessor::File::Directory{
        .contents = valueAt(obj, "contents"),
    };
}

void adl_serializer<MemorySourceAccessor::File::Directory>::to_json(
    json & json, const MemorySourceAccessor::File::Directory & val)
{
    json = {
        {"contents", val.contents},
    };
}

MemorySourceAccessor::File::Symlink adl_serializer<MemorySourceAccessor::File::Symlink>::from_json(const json & json)
{
    auto & obj = getObject(json);
    return MemorySourceAccessor::File::Symlink{
        .target = getString(valueAt(obj, "target")),
    };
}

void adl_serializer<MemorySourceAccessor::File::Symlink>::to_json(
    json & json, const MemorySourceAccessor::File::Symlink & val)
{
    json = {
        {"target", val.target},
    };
}

MemorySourceAccessor::File adl_serializer<MemorySourceAccessor::File>::from_json(const json & json)
{
    auto & obj = getObject(json);
    auto type = getString(valueAt(obj, "type"));
    if (type == "regular")
        return static_cast<MemorySourceAccessor::File::Regular>(json);
    if (type == "directory")
        return static_cast<MemorySourceAccessor::File::Directory>(json);
    if (type == "symlink")
        return static_cast<MemorySourceAccessor::File::Symlink>(json);
    else
        throw Error("unknown type of file '%s'", type);
}

void adl_serializer<MemorySourceAccessor::File>::to_json(json & json, const MemorySourceAccessor::File & val)
{
    std::visit(
        overloaded{
            [&](const MemorySourceAccessor::File::Regular & r) {
                json = r;
                json["type"] = "regular";
            },
            [&](const MemorySourceAccessor::File::Directory & d) {
                json = d;
                json["type"] = "directory";
            },
            [&](const MemorySourceAccessor::File::Symlink & s) {
                json = s;
                json["type"] = "symlink";
            },
        },
        val.raw);
}

MemorySourceAccessor adl_serializer<MemorySourceAccessor>::from_json(const json & json)
{
    MemorySourceAccessor res;
    res.root = json;
    return res;
}

void adl_serializer<MemorySourceAccessor>::to_json(json & json, const MemorySourceAccessor & val)
{
    json = val.root;
}

} // namespace nlohmann

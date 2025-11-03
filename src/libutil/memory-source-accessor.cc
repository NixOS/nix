#include "nix/util/memory-source-accessor.hh"

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
        return r->contents;
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

SourcePath MemorySourceAccessor::addFile(CanonPath path, std::string && contents)
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

void CreateMemoryRegularFile::operator()(std::string_view data)
{
    regularFile.contents += data;
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

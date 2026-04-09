#include "nix/util/memory-source-accessor.hh"

#include <ranges>

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

    unsigned j = 0;
    for (std::string_view name : path) {
        auto * curDirP = std::get_if<File::Directory>(&cur->raw);
        if (!curDirP) {
            CanonPath curDirPath = CanonPath::root;
            for (auto name2 : std::views::take(path, j))
                curDirPath.push(name2);
            if (std::holds_alternative<File::Symlink>(cur->raw))
                throw SymlinkNotAllowed(curDirPath, "file '%s' is a symlink", showPath(curDirPath));
            if (create)
                throw NotADirectory("file '%s' is not a directory", showPath(curDirPath));
            return nullptr;
        }

        auto & curDir = *curDirP;

        auto i = curDir.entries.find(name);
        if (i == curDir.entries.end()) {
            if (!create)
                return nullptr;
            else {
                newF = true;
                i = curDir.entries.insert(
                    i,
                    {
                        std::string{name},
                        File::Directory{},
                    });
            }
        }
        cur = &i->second;
        ++j;
    }

    if (newF && create)
        *cur = std::move(*create);

    return cur;
}

void MemorySourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
{
    auto * f = open(path, std::nullopt);
    if (!f)
        throw FileNotFound("file '%s' does not exist", showPath(path));
    std::visit(
        overloaded{
            [&](const File::Regular & r) {
                sizeCallback(r.contents.size());
                StringSource source{r.contents};
                source.drainInto(sink);
            },
            [&](const File::Directory &) { throw NotARegularFile("file '%s' is not a regular file", showPath(path)); },
            [&](const File::Symlink &) { throw SymlinkNotAllowed(path, "file '%s' is a symlink", showPath(path)); },
        },
        f->raw);
}

bool MemorySourceAccessor::pathExists(const CanonPath & path)
{
    return open(path, std::nullopt);
}

template<>
SourceAccessor::Stat MemorySourceAccessor::File::lstat() const
{
    return std::visit(
        overloaded{
            [](const Regular & r) {
                return SourceAccessor::Stat{
                    .type = SourceAccessor::tRegular,
                    .fileSize = r.contents.size(),
                    .isExecutable = r.executable,
                };
            },
            [](const Directory &) {
                return SourceAccessor::Stat{
                    .type = SourceAccessor::tDirectory,
                };
            },
            [](const Symlink &) {
                return SourceAccessor::Stat{
                    .type = SourceAccessor::tSymlink,
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
        throw FileNotFound("file '%s' does not exist", showPath(path));
    return std::visit(
        overloaded{
            [&](const File::Directory & d) {
                DirEntries res;
                for (auto & [name, file] : d.entries)
                    res.insert_or_assign(name, file.lstat().type);
                return res;
            },
            [&](const File::Regular &) -> DirEntries {
                throw NotADirectory("file '%s' is not a directory", showPath(path));
            },
            [&](const File::Symlink &) -> DirEntries {
                throw SymlinkNotAllowed(path, "file '%s' is a symlink", showPath(path));
            },
        },
        f->raw);
}

std::string MemorySourceAccessor::readLink(const CanonPath & path)
{
    auto * f = open(path, std::nullopt);
    if (!f)
        throw FileNotFound("file '%s' does not exist", showPath(path));
    if (auto * s = std::get_if<File::Symlink>(&f->raw))
        return s->target;
    else
        throw NotASymlink("file '%s' is not a symbolic link", showPath(path));
}

SourcePath MemorySourceAccessor::addFile(CanonPath path, std::string && contents)
{
    // Create root directory automatically if necessary as a convenience.
    if (!root && !path.isRoot())
        open(CanonPath::root, File::Directory{});

    MemorySourceAccessor::File * f = nullptr;
    try {
        f = open(path, File{File::Regular{}});
        if (!f)
            throw Error("file '%s' cannot be created because some parent directories don't exist", showPath(path));
    } catch (SourceAccessorError & e) {
        e.addTrace({}, "while creating file '%s'", showPath(path));
        throw;
    }
    if (auto * r = std::get_if<File::Regular>(&f->raw))
        r->contents = std::move(contents);
    else
        throw NotARegularFile("file '%s' is not a regular file", showPath(path));

    return SourcePath{ref(shared_from_this()), path};
}

using File = MemorySourceAccessor::File;

void MemorySink::createDirectory(const CanonPath & path)
{
    MemorySourceAccessor::File * f = nullptr;
    try {
        f = dst.open(path, File{File::Directory{}});
        if (!f)
            throw Error(
                "directory '%s' cannot be created because some parent directories don't exist", dst.showPath(path));
    } catch (SourceAccessorError & e) {
        e.addTrace({}, "while creating directory '%s'", dst.showPath(path));
        throw;
    }
    if (!std::holds_alternative<File::Directory>(f->raw))
        throw NotADirectory("file '%s' is not a directory", dst.showPath(path));
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

void MemorySink::createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)> func)
{
    MemorySourceAccessor::File * f = nullptr;
    try {
        f = dst.open(path, File{File::Regular{}});
        if (!f)
            throw Error("file '%s' cannot be created because some parent directories don't exist", dst.showPath(path));
    } catch (SourceAccessorError & e) {
        e.addTrace({}, "while creating regular file '%s'", dst.showPath(path));
        throw;
    }
    if (auto * rp = std::get_if<File::Regular>(&f->raw)) {
        CreateMemoryRegularFile crf{*rp};
        func(crf);
    } else
        throw NotARegularFile("file '%s' is not a regular file", dst.showPath(path));
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
    MemorySourceAccessor::File * f = nullptr;
    try {
        f = dst.open(path, File{File::Symlink{}});
        if (!f)
            throw Error(
                "symlink '%s' cannot be created because some parent directories don't exist", dst.showPath(path));
    } catch (SourceAccessorError & e) {
        e.addTrace({}, "while creating symlink '%s'", dst.showPath(path));
        throw;
    }
    if (auto * s = std::get_if<File::Symlink>(&f->raw))
        s->target = target;
    else
        throw NotASymlink("file '%s' is not a symbolic link", dst.showPath(path));
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

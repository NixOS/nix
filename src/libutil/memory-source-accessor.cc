#include "nix/util/memory-source-accessor.hh"
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
    if (auto * r = std::get_if<File::Regular>(&f->raw)) {
        sizeCallback(r->contents.size());
        StringSource source{r->contents};
        source.drainInto(sink);
    } else
        throw NotARegularFile("file '%s' is not a regular file", showPath(path));
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
    if (auto * d = std::get_if<File::Directory>(&f->raw)) {
        DirEntries res;
        for (auto & [name, file] : d->entries)
            res.insert_or_assign(name, file.lstat().type);
        return res;
    } else
        throw NotADirectory("file '%s' is not a directory", showPath(path));
    return {};
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

    auto * f = open(path, File{File::Regular{}});
    if (!f)
        throw Error("file '%s' cannot be created because some parent file is not a directory", showPath(path));
    if (auto * r = std::get_if<File::Regular>(&f->raw))
        r->contents = std::move(contents);
    else
        throw NotARegularFile("file '%s' is not a regular file", showPath(path));

    return SourcePath{ref(shared_from_this()), path};
}

using File = MemorySourceAccessor::File;

void MemorySink::MemoryDirectory::createChild(std::string_view name, ChildCreatedCallback callback)
{
    auto [it, inserted] = dir.entries.emplace(std::string{name}, File::Directory{});
    MemorySink childSink{[&](File file) -> File & {
        it->second = std::move(file);
        return it->second;
    }};
    callback(childSink);
}

void MemorySink::createDirectory(DirectoryCreatedCallback callback)
{
    auto & f = createRoot(File::Directory{});
    auto * dirP = std::get_if<File::Directory>(&f.raw);
    if (!dirP)
        throw Error("cannot create directory: not a directory");

    MemoryDirectory dir{*dirP};
    callback(dir);
}

struct CreateMemoryRegularFile : FileSystemObjectSink::OnRegularFile
{
    File::Regular & regularFile;

    CreateMemoryRegularFile(File::Regular & r)
        : regularFile(r)
    {
    }

    void operator()(std::string_view data) override;
    void preallocateContents(uint64_t size) override;
};

void MemorySink::createRegularFile(bool isExecutable, RegularFileCreatedCallback func)
{
    auto & f = createRoot(File::Regular{.executable = isExecutable});
    if (auto * rp = std::get_if<File::Regular>(&f.raw)) {
        CreateMemoryRegularFile crf{*rp};
        func(crf);
    } else
        throw Error("cannot create regular file: not a regular file");
}

void CreateMemoryRegularFile::preallocateContents(uint64_t len)
{
    regularFile.contents.reserve(len);
}

void CreateMemoryRegularFile::operator()(std::string_view data)
{
    regularFile.contents += data;
}

void MemorySink::createSymlink(const std::string & target)
{
    auto & f = createRoot(File::Symlink{.target = target});
    if (!std::holds_alternative<File::Symlink>(f.raw))
        throw Error("cannot create symlink: not a symlink");
}

ref<SourceAccessor> makeEmptySourceAccessor()
{
    static auto empty = []() {
        auto empty = make_ref<MemorySourceAccessor>();
        MemorySink sink{[&](File file) -> File & { return empty->root.emplace(std::move(file)); }};
        sink.createDirectory([](auto &) {});
        /* Don't forget to clear the display prefix, as the default constructed
           SourceAccessor has the «unknown» prefix. Since this accessor is supposed
           to mimic an empty root directory the prefix needs to be empty. */
        empty->setPathDisplay("");
        return empty.cast<SourceAccessor>();
    }();
    return empty;
}

} // namespace nix

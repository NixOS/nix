#include "source-accessor.hh"
#include "archive.hh"

namespace nix {

static std::atomic<size_t> nextNumber{0};

SourceAccessor::SourceAccessor()
    : number(++nextNumber)
    , displayPrefix{"«unknown»"}
{
}

std::string SourceAccessor::readFile(const CanonPath & path)
{
    StringSink sink;
    std::optional<uint64_t> size;
    readFile(path, sink, [&](uint64_t _size)
    {
        size = _size;
    });
    assert(size && *size == sink.s.size());
    return std::move(sink.s);
}

void SourceAccessor::readFile(
    const CanonPath & path,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback)
{
    auto s = readFile(path);
    sizeCallback(s.size());
    sink(s);
}

Hash SourceAccessor::hashPath(
    const CanonPath & path,
    PathFilter & filter,
    HashType ht)
{
    HashSink sink(ht);
    dumpPath(path, sink, filter);
    return sink.finish().first;
}

std::optional<SourceAccessor::Stat> SourceAccessor::maybeLstat(const CanonPath & path)
{
    // FIXME: merge these into one operation.
    if (!pathExists(path))
        return {};
    return lstat(path);
}

void SourceAccessor::setPathDisplay(std::string displayPrefix, std::string displaySuffix)
{
    this->displayPrefix = std::move(displayPrefix);
    this->displaySuffix = std::move(displaySuffix);
}

std::string SourceAccessor::showPath(const CanonPath & path)
{
    return displayPrefix + path.abs() + displaySuffix;
}

}

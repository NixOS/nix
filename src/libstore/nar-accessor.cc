#include "nar-accessor.hh"
#include "archive.hh"

#include <map>

namespace nix {

struct NarMember
{
    FSAccessor::Type type;

    bool isExecutable;

    /* If this is a regular file, position of the contents of this
       file in the NAR. */
    size_t start, size;

    std::string target;
};

struct NarIndexer : ParseSink, StringSource
{
    // FIXME: should store this as a tree. Now we're vulnerable to
    // O(nm) memory consumption (e.g. for x_0/.../x_n/{y_0..y_m}).
    typedef std::map<Path, NarMember> Members;
    Members members;

    Path currentPath;
    std::string currentStart;
    bool isExec = false;

    NarIndexer(const std::string & nar) : StringSource(nar)
    {
    }

    void createDirectory(const Path & path) override
    {
        members.emplace(path,
            NarMember{FSAccessor::Type::tDirectory, false, 0, 0});
    }

    void createRegularFile(const Path & path) override
    {
        currentPath = path;
    }

    void isExecutable() override
    {
        isExec = true;
    }

    void preallocateContents(unsigned long long size) override
    {
        currentStart = string(s, pos, 16);
        members.emplace(currentPath,
            NarMember{FSAccessor::Type::tRegular, isExec, pos, size});
    }

    void receiveContents(unsigned char * data, unsigned int len) override
    {
        // Sanity check
        if (!currentStart.empty()) {
            assert(len < 16 || currentStart == string((char *) data, 16));
            currentStart.clear();
        }
    }

    void createSymlink(const Path & path, const string & target) override
    {
        members.emplace(path,
            NarMember{FSAccessor::Type::tSymlink, false, 0, 0, target});
    }

    Members::iterator find(const Path & path)
    {
        auto i = members.find(path);
        if (i == members.end())
            throw Error(format("NAR file does not contain path ‘%1%’") % path);
        return i;
    }
};

struct NarAccessor : public FSAccessor
{
    ref<const std::string> nar;
    NarIndexer indexer;

    NarAccessor(ref<const std::string> nar) : nar(nar), indexer(*nar)
    {
        parseDump(indexer, indexer);
    }

    Stat stat(const Path & path) override
    {
        auto i = indexer.members.find(path);
        if (i == indexer.members.end())
            return {FSAccessor::Type::tMissing, 0, false};
        return {i->second.type, i->second.size, i->second.isExecutable};
    }

    StringSet readDirectory(const Path & path) override
    {
        auto i = indexer.find(path);

        if (i->second.type != FSAccessor::Type::tDirectory)
            throw Error(format("path ‘%1%’ inside NAR file is not a directory") % path);

        ++i;
        StringSet res;
        while (i != indexer.members.end() && isInDir(i->first, path)) {
            // FIXME: really bad performance.
            if (i->first.find('/', path.size() + 1) == std::string::npos)
                res.insert(std::string(i->first, path.size() + 1));
            ++i;
        }
        return res;
    }

    std::string readFile(const Path & path) override
    {
        auto i = indexer.find(path);
        if (i->second.type != FSAccessor::Type::tRegular)
            throw Error(format("path ‘%1%’ inside NAR file is not a regular file") % path);
        return std::string(*nar, i->second.start, i->second.size);
    }

    std::string readLink(const Path & path) override
    {
        auto i = indexer.find(path);
        if (i->second.type != FSAccessor::Type::tSymlink)
            throw Error(format("path ‘%1%’ inside NAR file is not a symlink") % path);
        return i->second.target;
    }
};

ref<FSAccessor> makeNarAccessor(ref<const std::string> nar)
{
    return make_ref<NarAccessor>(nar);
}

}

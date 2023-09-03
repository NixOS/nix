#include "nar-accessor.hh"
#include "archive.hh"

#include <map>
#include <stack>
#include <algorithm>

#include <nlohmann/json.hpp>

namespace nix {

struct NarMember
{
    FSAccessor::Type type = FSAccessor::Type::tMissing;

    bool isExecutable = false;

    /* If this is a regular file, position of the contents of this
       file in the NAR. */
    uint64_t start = 0, size = 0;

    std::string target;

    /* If this is a directory, all the children of the directory. */
    std::map<std::string, NarMember> children;
};

struct NarAccessor : public FSAccessor
{
    std::optional<const std::string> nar;

    GetNarBytes getNarBytes;

    NarMember root;

    struct NarIndexer : ParseSink, Source
    {
        NarAccessor & acc;
        Source & source;

        std::stack<NarMember *> parents;

        bool isExec = false;

        uint64_t pos = 0;

        NarIndexer(NarAccessor & acc, Source & source)
            : acc(acc), source(source)
        { }

        void createMember(const Path & path, NarMember member)
        {
            size_t level = std::count(path.begin(), path.end(), '/');
            while (parents.size() > level) parents.pop();

            if (parents.empty()) {
                acc.root = std::move(member);
                parents.push(&acc.root);
            } else {
                if (parents.top()->type != FSAccessor::Type::tDirectory)
                    throw Error("NAR file missing parent directory of path '%s'", path);
                auto result = parents.top()->children.emplace(baseNameOf(path), std::move(member));
                parents.push(&result.first->second);
            }
        }

        void createDirectory(const Path & path) override
        {
            createMember(path, {FSAccessor::Type::tDirectory, false, 0, 0});
        }

        void createRegularFile(const Path & path) override
        {
            createMember(path, {FSAccessor::Type::tRegular, false, 0, 0});
        }

        void closeRegularFile() override
        { }

        void isExecutable() override
        {
            parents.top()->isExecutable = true;
        }

        void preallocateContents(uint64_t size) override
        {
            assert(size <= std::numeric_limits<uint64_t>::max());
            parents.top()->size = (uint64_t) size;
            parents.top()->start = pos;
        }

        void receiveContents(std::string_view data) override
        { }

        void createSymlink(const Path & path, const std::string & target) override
        {
            createMember(path,
                NarMember{FSAccessor::Type::tSymlink, false, 0, 0, target});
        }

        size_t read(char * data, size_t len) override
        {
            auto n = source.read(data, len);
            pos += n;
            return n;
        }
    };

    NarAccessor(std::string && _nar) : nar(_nar)
    {
        StringSource source(*nar);
        NarIndexer indexer(*this, source);
        parseDump(indexer, indexer);
    }

    NarAccessor(Source & source)
    {
        NarIndexer indexer(*this, source);
        parseDump(indexer, indexer);
    }

    NarAccessor(const std::string & listing, GetNarBytes getNarBytes)
        : getNarBytes(getNarBytes)
    {
        using json = nlohmann::json;

        std::function<void(NarMember &, json &)> recurse;

        recurse = [&](NarMember & member, json & v) {
            std::string type = v["type"];

            if (type == "directory") {
                member.type = FSAccessor::Type::tDirectory;
                for (auto i = v["entries"].begin(); i != v["entries"].end(); ++i) {
                    std::string name = i.key();
                    recurse(member.children[name], i.value());
                }
            } else if (type == "regular") {
                member.type = FSAccessor::Type::tRegular;
                member.size = v["size"];
                member.isExecutable = v.value("executable", false);
                member.start = v["narOffset"];
            } else if (type == "symlink") {
                member.type = FSAccessor::Type::tSymlink;
                member.target = v.value("target", "");
            } else return;
        };

        json v = json::parse(listing);
        recurse(root, v);
    }

    NarMember * find(const Path & path)
    {
        Path canon = path == "" ? "" : canonPath(path);
        NarMember * current = &root;
        auto end = path.end();
        for (auto it = path.begin(); it != end; ) {
            // because it != end, the remaining component is non-empty so we need
            // a directory
            if (current->type != FSAccessor::Type::tDirectory) return nullptr;

            // skip slash (canonPath above ensures that this is always a slash)
            assert(*it == '/');
            it += 1;

            // lookup current component
            auto next = std::find(it, end, '/');
            auto child = current->children.find(std::string(it, next));
            if (child == current->children.end()) return nullptr;
            current = &child->second;

            it = next;
        }

        return current;
    }

    NarMember & get(const Path & path) {
        auto result = find(path);
        if (result == nullptr)
            throw Error("NAR file does not contain path '%1%'", path);
        return *result;
    }

    Stat stat(const Path & path) override
    {
        auto i = find(path);
        if (i == nullptr)
            return {FSAccessor::Type::tMissing, 0, false};
        return {i->type, i->size, i->isExecutable, i->start};
    }

    StringSet readDirectory(const Path & path) override
    {
        auto i = get(path);

        if (i.type != FSAccessor::Type::tDirectory)
            throw Error("path '%1%' inside NAR file is not a directory", path);

        StringSet res;
        for (auto & child : i.children)
            res.insert(child.first);

        return res;
    }

    std::string readFile(const Path & path, bool requireValidPath = true) override
    {
        auto i = get(path);
        if (i.type != FSAccessor::Type::tRegular)
            throw Error("path '%1%' inside NAR file is not a regular file", path);

        if (getNarBytes) return getNarBytes(i.start, i.size);

        assert(nar);
        return std::string(*nar, i.start, i.size);
    }

    std::string readLink(const Path & path) override
    {
        auto i = get(path);
        if (i.type != FSAccessor::Type::tSymlink)
            throw Error("path '%1%' inside NAR file is not a symlink", path);
        return i.target;
    }
};

ref<FSAccessor> makeNarAccessor(std::string && nar)
{
    return make_ref<NarAccessor>(std::move(nar));
}

ref<FSAccessor> makeNarAccessor(Source & source)
{
    return make_ref<NarAccessor>(source);
}

ref<FSAccessor> makeLazyNarAccessor(const std::string & listing,
    GetNarBytes getNarBytes)
{
    return make_ref<NarAccessor>(listing, getNarBytes);
}

using nlohmann::json;
json listNar(ref<FSAccessor> accessor, const Path & path, bool recurse)
{
    auto st = accessor->stat(path);

    json obj = json::object();

    switch (st.type) {
    case FSAccessor::Type::tRegular:
        obj["type"] = "regular";
        obj["size"] = st.fileSize;
        if (st.isExecutable)
            obj["executable"] = true;
        if (st.narOffset)
            obj["narOffset"] = st.narOffset;
        break;
    case FSAccessor::Type::tDirectory:
        obj["type"] = "directory";
        {
            obj["entries"] = json::object();
            json &res2 = obj["entries"];
            for (auto & name : accessor->readDirectory(path)) {
                if (recurse) {
                    res2[name] = listNar(accessor, path + "/" + name, true);
                } else
                    res2[name] = json::object();
            }
        }
        break;
    case FSAccessor::Type::tSymlink:
        obj["type"] = "symlink";
        obj["target"] = accessor->readLink(path);
        break;
    default:
        throw Error("path '%s' does not exist in NAR", path);
    }
    return obj;
}

}

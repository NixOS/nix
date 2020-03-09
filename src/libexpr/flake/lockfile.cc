#include "lockfile.hh"
#include "store-api.hh"
#include "fetchers/regex.hh"

#include <nlohmann/json.hpp>

namespace nix::flake {

FlakeRef flakeRefFromJson(const nlohmann::json & json)
{
    return FlakeRef::fromAttrs(jsonToAttrs(json));
}

FlakeRef getFlakeRef(
    const nlohmann::json & json,
    const char * version3Attr1,
    const char * version3Attr2,
    const char * version4Attr)
{
    auto i = json.find(version4Attr);
    if (i != json.end())
        return flakeRefFromJson(*i);

    // FIXME: remove these.
    i = json.find(version3Attr1);
    if (i != json.end())
        return parseFlakeRef(*i);

    i = json.find(version3Attr2);
    if (i != json.end())
        return parseFlakeRef(*i);

    throw Error("attribute '%s' missing in lock file", version4Attr);
}

static TreeInfo parseTreeInfo(const nlohmann::json & json)
{
    TreeInfo info;

    auto i = json.find("info");
    if (i != json.end()) {
        const nlohmann::json & i2(*i);

        auto j = i2.find("narHash");
        if (j != i2.end())
            info.narHash = Hash((std::string) *j);
        else
            throw Error("attribute 'narHash' missing in lock file");

        j = i2.find("revCount");
        if (j != i2.end())
            info.revCount = *j;

        j = i2.find("lastModified");
        if (j != i2.end())
            info.lastModified = *j;

        return info;
    }

    i = json.find("narHash");
    if (i != json.end()) {
        info.narHash = Hash((std::string) *i);
        return info;
    }

    throw Error("attribute 'info' missing in lock file");
}

LockedInput::LockedInput(const nlohmann::json & json)
    : LockedInputs(json)
    , lockedRef(getFlakeRef(json, "url", "uri", "locked"))
    , originalRef(getFlakeRef(json, "originalUrl", "originalUri", "original"))
    , info(parseTreeInfo(json))
    , isFlake(json.find("flake") != json.end() ? (bool) json["flake"] : true)
{
    if (!lockedRef.input->isImmutable())
        throw Error("lockfile contains mutable flakeref '%s'", lockedRef);
}

static nlohmann::json treeInfoToJson(const TreeInfo & info)
{
    nlohmann::json json;
    assert(info.narHash);
    json["narHash"] = info.narHash.to_string(SRI);
    if (info.revCount)
        json["revCount"] = *info.revCount;
    if (info.lastModified)
        json["lastModified"] = *info.lastModified;
    return json;
}

nlohmann::json LockedInput::toJson() const
{
    auto json = LockedInputs::toJson();
    json["original"] = fetchers::attrsToJson(originalRef.toAttrs());
    json["locked"] = fetchers::attrsToJson(lockedRef.toAttrs());
    json["info"] = treeInfoToJson(info);
    if (!isFlake) json["flake"] = false;
    return json;
}

StorePath LockedInput::computeStorePath(Store & store) const
{
    return info.computeStorePath(store);
}

LockedInputs::LockedInputs(const nlohmann::json & json)
{
    for (auto & i : json["inputs"].items())
        inputs.insert_or_assign(i.key(), LockedInput(i.value()));
}

nlohmann::json LockedInputs::toJson() const
{
    nlohmann::json json;
    {
        auto j = nlohmann::json::object();
        for (auto & i : inputs)
            j[i.first] = i.second.toJson();
        json["inputs"] = std::move(j);
    }
    return json;
}

bool LockedInputs::isImmutable() const
{
    for (auto & i : inputs)
        if (!i.second.lockedRef.input->isImmutable() || !i.second.isImmutable()) return false;

    return true;
}

std::optional<LockedInput *> LockedInputs::findInput(const InputPath & path)
{
    assert(!path.empty());

    LockedInputs * pos = this;

    for (auto & elem : path) {
        auto i = pos->inputs.find(elem);
        if (i == pos->inputs.end())
            return {};
        pos = &i->second;
    }

    return (LockedInput *) pos;
}

void LockedInputs::removeInput(const InputPath & path)
{
    assert(!path.empty());

    LockedInputs * pos = this;

    for (size_t n = 0; n < path.size(); n++) {
        auto i = pos->inputs.find(path[n]);
        if (i == pos->inputs.end()) return;
        if (n + 1 == path.size())
            pos->inputs.erase(i);
        else
            pos = &i->second;
    }
}

nlohmann::json LockFile::toJson() const
{
    auto json = LockedInputs::toJson();
    json["version"] = 4;
    return json;
}

LockFile LockFile::read(const Path & path)
{
    if (pathExists(path)) {
        auto json = nlohmann::json::parse(readFile(path));

        auto version = json.value("version", 0);
        if (version != 3 && version != 4)
            throw Error("lock file '%s' has unsupported version %d", path, version);

        return LockFile(json);
    } else
        return LockFile();
}

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile)
{
    stream << lockFile.toJson().dump(2);
    return stream;
}

void LockFile::write(const Path & path) const
{
    createDirs(dirOf(path));
    writeFile(path, fmt("%s\n", *this));
}

InputPath parseInputPath(std::string_view s)
{
    InputPath path;

    for (auto & elem : tokenizeString<std::vector<std::string>>(s, "/")) {
        if (!std::regex_match(elem, fetchers::flakeIdRegex))
            throw Error("invalid flake input path element '%s'", elem);
        path.push_back(elem);
    }

    if (path.empty())
        throw Error("flake input path is empty");

    return path;
}

}

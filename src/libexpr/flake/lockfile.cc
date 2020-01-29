#include "lockfile.hh"
#include "store-api.hh"
#include "fetchers/regex.hh"

#include <nlohmann/json.hpp>

namespace nix::flake {

LockedInput::LockedInput(const nlohmann::json & json)
    : LockedInputs(json)
    , ref(parseFlakeRef(json.value("url", json.value("uri", ""))))
    , originalRef(parseFlakeRef(json.value("originalUrl", json.value("originalUri", ""))))
    , narHash(Hash((std::string) json["narHash"]))
{
    if (!ref.isImmutable())
        throw Error("lockfile contains mutable flakeref '%s'", ref);
}

nlohmann::json LockedInput::toJson() const
{
    auto json = LockedInputs::toJson();
    json["url"] = ref.to_string();
    json["originalUrl"] = originalRef.to_string();
    json["narHash"] = narHash.to_string(SRI);
    return json;
}

Path LockedInput::computeStorePath(Store & store) const
{
    return store.printStorePath(store.makeFixedOutputPath(true, narHash, "source"));
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
        if (!i.second.ref.isImmutable() || !i.second.isImmutable()) return false;

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
    json["version"] = 3;
    return json;
}

LockFile LockFile::read(const Path & path)
{
    if (pathExists(path)) {
        auto json = nlohmann::json::parse(readFile(path));

        auto version = json.value("version", 0);
        if (version != 3)
            throw Error("lock file '%s' has unsupported version %d", path, version);

        return LockFile(json);
    } else
        return LockFile();
}

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile)
{
    stream << lockFile.toJson().dump(4); // '4' = indentation in json file
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

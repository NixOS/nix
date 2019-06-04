#include "lockfile.hh"
#include "store-api.hh"

namespace nix::flake {

AbstractDep::AbstractDep(const nlohmann::json & json)
    : ref(json["uri"])
    , narHash(Hash((std::string) json["narHash"]))
{
    if (!ref.isImmutable())
        throw Error("lockfile contains mutable flakeref '%s'", ref);
}

nlohmann::json AbstractDep::toJson() const
{
    nlohmann::json json;
    json["uri"] = ref.to_string();
    json["narHash"] = narHash.to_string(SRI);
    return json;
}

Path AbstractDep::computeStorePath(Store & store) const
{
    return store.makeFixedOutputPath(true, narHash, "source");
}

FlakeDep::FlakeDep(const nlohmann::json & json)
    : FlakeInputs(json)
    , AbstractDep(json)
    , id(json["id"])
{
}

nlohmann::json FlakeDep::toJson() const
{
    auto json = FlakeInputs::toJson();
    json.update(AbstractDep::toJson());
    json["id"] = id;
    return json;
}

FlakeInputs::FlakeInputs(const nlohmann::json & json)
{
    auto nonFlakeInputs = json["nonFlakeInputs"];
    for (auto i = nonFlakeInputs.begin(); i != nonFlakeInputs.end(); ++i)
        nonFlakeDeps.insert_or_assign(i.key(), NonFlakeDep(*i));

    auto inputs = json["inputs"];
    for (auto i = inputs.begin(); i != inputs.end(); ++i)
        flakeDeps.insert_or_assign(i.key(), FlakeDep(*i));
}

nlohmann::json FlakeInputs::toJson() const
{
    nlohmann::json json;
    {
        auto j = nlohmann::json::object();
        for (auto & i : nonFlakeDeps)
            j[i.first] = i.second.toJson();
        json["nonFlakeInputs"] = std::move(j);
    }
    {
        auto j = nlohmann::json::object();
        for (auto & i : flakeDeps)
            j[i.first.to_string()] = i.second.toJson();
        json["inputs"] = std::move(j);
    }
    return json;
}

nlohmann::json LockFile::toJson() const
{
    auto json = FlakeInputs::toJson();
    json["version"] = 2;
    return json;
}

LockFile LockFile::read(const Path & path)
{
    if (pathExists(path)) {
        auto json = nlohmann::json::parse(readFile(path));

        auto version = json.value("version", 0);
        if (version != 2)
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

}

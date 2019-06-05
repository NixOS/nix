#include "lockfile.hh"
#include "store-api.hh"

namespace nix::flake {

AbstractInput::AbstractInput(const nlohmann::json & json)
    : ref(json["uri"])
    , narHash(Hash((std::string) json["narHash"]))
{
    if (!ref.isImmutable())
        throw Error("lockfile contains mutable flakeref '%s'", ref);
}

nlohmann::json AbstractInput::toJson() const
{
    nlohmann::json json;
    json["uri"] = ref.to_string();
    json["narHash"] = narHash.to_string(SRI);
    return json;
}

Path AbstractInput::computeStorePath(Store & store) const
{
    return store.makeFixedOutputPath(true, narHash, "source");
}

FlakeInput::FlakeInput(const nlohmann::json & json)
    : FlakeInputs(json)
    , AbstractInput(json)
    , id(json["id"])
{
}

nlohmann::json FlakeInput::toJson() const
{
    auto json = FlakeInputs::toJson();
    json.update(AbstractInput::toJson());
    json["id"] = id;
    return json;
}

FlakeInputs::FlakeInputs(const nlohmann::json & json)
{
    for (auto & i : json["nonFlakeInputs"].items())
        nonFlakeInputs.insert_or_assign(i.key(), NonFlakeInput(i.value()));

    for (auto & i : json["inputs"].items())
        flakeInputs.insert_or_assign(i.key(), FlakeInput(i.value()));
}

nlohmann::json FlakeInputs::toJson() const
{
    nlohmann::json json;
    {
        auto j = nlohmann::json::object();
        for (auto & i : nonFlakeInputs)
            j[i.first] = i.second.toJson();
        json["nonFlakeInputs"] = std::move(j);
    }
    {
        auto j = nlohmann::json::object();
        for (auto & i : flakeInputs)
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

#include "lockfile.hh"
#include "store-api.hh"

namespace nix::flake {

LockedInput::LockedInput(const nlohmann::json & json)
    : LockedInputs(json)
    , ref(json["uri"])
    , narHash(Hash((std::string) json["narHash"]))
{
    if (!ref.isImmutable())
        throw Error("lockfile contains mutable flakeref '%s'", ref);
}

nlohmann::json LockedInput::toJson() const
{
    auto json = LockedInputs::toJson();
    json["uri"] = ref.to_string();
    json["narHash"] = narHash.to_string(SRI);
    return json;
}

Path LockedInput::computeStorePath(Store & store) const
{
    return store.makeFixedOutputPath(true, narHash, "source");
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

bool LockedInputs::isDirty() const
{
    for (auto & i : inputs)
        if (i.second.ref.isDirty() || i.second.isDirty()) return true;

    return false;
}

nlohmann::json LockFile::toJson() const
{
    auto json = LockedInputs::toJson();
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

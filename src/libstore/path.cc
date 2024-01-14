#include "store-dir-config.hh"

namespace nix {

static void checkName(std::string_view path, std::string_view name)
{
    if (name.empty())
        throw BadStorePath("store path '%s' has an empty name", path);
    if (name.size() > StorePath::MaxPathLen)
        throw BadStorePath("store path '%s' has a name longer than %d characters",
            path, StorePath::MaxPathLen);
    // See nameRegexStr for the definition
    if (name[0] == '.') {
        // check against "." and "..", followed by end or dash
        if (name.size() == 1)
            throw BadStorePath("store path '%s' has invalid name '%s'", path, name);
        if (name[1] == '-')
            throw BadStorePath("store path '%s' has invalid name '%s': first dash-separated component must not be '%s'", path, name, ".");
        if (name[1] == '.') {
            if (name.size() == 2)
                throw BadStorePath("store path '%s' has invalid name '%s'", path, name);
            if (name[2] == '-')
                throw BadStorePath("store path '%s' has invalid name '%s': first dash-separated component must not be '%s'", path, name, "..");
        }
    }
    for (auto c : name)
        if (!((c >= '0' && c <= '9')
                || (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || c == '+' || c == '-' || c == '.' || c == '_' || c == '?' || c == '='))
            throw BadStorePath("store path '%s' contains illegal character '%s'", path, c);
}

StorePath::StorePath(std::string_view _baseName)
    : baseName(_baseName)
{
    if (baseName.size() < HashLen + 1)
        throw BadStorePath("'%s' is too short to be a valid store path", baseName);
    for (auto c : hashPart())
        if (c == 'e' || c == 'o' || c == 'u' || c == 't'
            || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
            throw BadStorePath("store path '%s' contains illegal base-32 character '%s'", baseName, c);
    checkName(baseName, name());
}

StorePath::StorePath(const Hash & hash, std::string_view _name)
    : baseName((hash.to_string(HashFormat::Nix32, false) + "-").append(std::string(_name)))
{
    checkName(baseName, name());
}

bool StorePath::isDerivation() const
{
    return hasSuffix(name(), drvExtension);
}

StorePath StorePath::dummy("ffffffffffffffffffffffffffffffff-x");

StorePath StorePath::random(std::string_view name)
{
    return StorePath(Hash::random(HashAlgorithm::SHA1), name);
}

StorePath StoreDirConfig::parseStorePath(std::string_view path) const
{
    // On Windows, `/nix/store` is not a canonical path. More broadly it
    // is unclear whether this function should be using the native
    // notion of a canonical path at all. For example, it makes to
    // support remote stores whose store dir is a non-native path (e.g.
    // Windows <-> Unix ssh-ing).
    auto p =
#ifdef _WIN32
        path
#else
        canonPath(std::string(path))
#endif
        ;
    if (dirOf(p) != storeDir)
        throw BadStorePath("path '%s' is not in the Nix store", p);
    return StorePath(baseNameOf(p));
}

std::optional<StorePath> StoreDirConfig::maybeParseStorePath(std::string_view path) const
{
    try {
        return parseStorePath(path);
    } catch (Error &) {
        return {};
    }
}

bool StoreDirConfig::isStorePath(std::string_view path) const
{
    return (bool) maybeParseStorePath(path);
}

StorePathSet StoreDirConfig::parseStorePathSet(const PathSet & paths) const
{
    StorePathSet res;
    for (auto & i : paths) res.insert(parseStorePath(i));
    return res;
}

std::string StoreDirConfig::printStorePath(const StorePath & path) const
{
    return (storeDir + "/").append(path.to_string());
}

PathSet StoreDirConfig::printStorePathSet(const StorePathSet & paths) const
{
    PathSet res;
    for (auto & i : paths) res.insert(printStorePath(i));
    return res;
}

}

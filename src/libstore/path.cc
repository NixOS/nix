#include "nix/store/store-dir-config.hh"

namespace nix {

void checkName(std::string_view name)
{
    if (name.empty())
        throw BadStorePathName("name must not be empty");
    if (name.size() > StorePath::MaxPathLen)
        throw BadStorePathName("name '%s' must be no longer than %d characters", name, StorePath::MaxPathLen);
    // See nameRegexStr for the definition
    if (name[0] == '.') {
        // check against "." and "..", followed by end or dash
        if (name.size() == 1)
            throw BadStorePathName("name '%s' is not valid", name);
        if (name[1] == '-')
            throw BadStorePathName(
                "name '%s' is not valid: first dash-separated component must not be '%s'", name, ".");
        if (name[1] == '.') {
            if (name.size() == 2)
                throw BadStorePathName("name '%s' is not valid", name);
            if (name[2] == '-')
                throw BadStorePathName(
                    "name '%s' is not valid: first dash-separated component must not be '%s'", name, "..");
        }
    }
    for (auto c : name)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '+' || c == '-'
              || c == '.' || c == '_' || c == '?' || c == '='))
            throw BadStorePathName("name '%s' contains illegal character '%s'", name, c);
}

static void checkPathName(std::string_view path, std::string_view name)
{
    try {
        checkName(name);
    } catch (BadStorePathName & e) {
        throw BadStorePath("path '%s' is not a valid store path: %s", path, Uncolored(e.message()));
    }
}

StorePath::StorePath(std::string_view _baseName)
    : baseName(_baseName)
{
    if (baseName.size() < HashLen + 1)
        throw BadStorePath("'%s' is too short to be a valid store path", baseName);
    for (auto c : hashPart())
        if (c == 'e' || c == 'o' || c == 'u' || c == 't' || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
            throw BadStorePath("store path '%s' contains illegal base-32 character '%s'", baseName, c);
    checkPathName(baseName, name());
}

StorePath::StorePath(const Hash & hash, std::string_view _name)
    : baseName((hash.to_string(HashFormat::Nix32, false) + "-").append(std::string(_name)))
{
    checkPathName(baseName, name());
}

bool StorePath::isDerivation() const noexcept
{
    return hasSuffix(name(), drvExtension);
}

void StorePath::requireDerivation() const
{
    if (!isDerivation())
        throw FormatError("store path '%s' is not a valid derivation path", to_string());
}

StorePath StorePath::dummy("ffffffffffffffffffffffffffffffff-x");

StorePath StorePath::random(std::string_view name)
{
    return StorePath(Hash::random(HashAlgorithm::SHA1), name);
}

StorePath MixStoreDirMethods::parseStorePath(std::string_view path) const
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

std::optional<StorePath> MixStoreDirMethods::maybeParseStorePath(std::string_view path) const
{
    try {
        return parseStorePath(path);
    } catch (Error &) {
        return {};
    }
}

bool MixStoreDirMethods::isStorePath(std::string_view path) const
{
    return (bool) maybeParseStorePath(path);
}

StorePathSet MixStoreDirMethods::parseStorePathSet(const PathSet & paths) const
{
    StorePathSet res;
    for (auto & i : paths)
        res.insert(parseStorePath(i));
    return res;
}

std::string MixStoreDirMethods::printStorePath(const StorePath & path) const
{
    return (storeDir + "/").append(path.to_string());
}

PathSet MixStoreDirMethods::printStorePathSet(const StorePathSet & paths) const
{
    PathSet res;
    for (auto & i : paths)
        res.insert(printStorePath(i));
    return res;
}

}

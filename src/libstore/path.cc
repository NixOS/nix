#include "store-api.hh"

namespace nix {

static void checkName(std::string_view path, std::string_view name)
{
    if (name.empty())
        throw BadStorePath("store path '%s' has an empty name", path);
    if (name.size() > 211)
        throw BadStorePath("store path '%s' has a name longer than 211 characters", path);
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
    : baseName((hash.to_string(Base32, false) + "-").append(std::string(_name)))
{
    checkName(baseName, name());
}

bool StorePath::isDerivation() const
{
    return hasSuffix(name(), drvExtension);
}

StorePath StorePath::dummy("ffffffffffffffffffffffffffffffff-x");

StorePath Store::parseStorePath(std::string_view path) const
{
    auto p = canonPath(std::string(path));
    if (dirOf(p) != storeDir)
        throw BadStorePath("path '%s' is not in the Nix store", p);
    return StorePath(baseNameOf(p));
}

std::optional<StorePath> Store::maybeParseStorePath(std::string_view path) const
{
    try {
        return parseStorePath(path);
    } catch (Error &) {
        return {};
    }
}

bool Store::isStorePath(std::string_view path) const
{
    return (bool) maybeParseStorePath(path);
}

StorePathSet Store::parseStorePathSet(const PathSet & paths) const
{
    StorePathSet res;
    for (auto & i : paths) res.insert(parseStorePath(i));
    return res;
}

std::string Store::printStorePath(const StorePath & path) const
{
    return (storeDir + "/").append(path.to_string());
}

PathSet Store::printStorePathSet(const StorePathSet & paths) const
{
    PathSet res;
    for (auto & i : paths) res.insert(printStorePath(i));
    return res;
}

std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s)
{
    size_t n = s.find("!");
    return n == s.npos
        ? std::make_pair(s, std::set<string>())
        : std::make_pair(((std::string_view) s).substr(0, n),
            tokenizeString<std::set<string>>(((std::string_view) s).substr(n + 1), ","));
}

StorePathWithOutputs Store::parsePathWithOutputs(const std::string & s)
{
    auto [path, outputs] = nix::parsePathWithOutputs(s);
    return {parseStorePath(path), std::move(outputs)};
}

}

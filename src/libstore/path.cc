#include <nlohmann/json.hpp>

#include "nix/store/store-dir-config.hh"
#include "nix/util/json-utils.hh"

namespace nix {

void BadStorePathName::anchor() {}

void BadStorePath::anchor() {}

void checkName(std::string_view name)
{
    if (name.empty())
        throw BadStorePathName("name must not be empty");
    if (name.size() > StorePath::MaxNameLen)
        throw BadStorePathName("name '%s' must be no longer than %d characters", name, StorePath::MaxNameLen);
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

/**
 * A checked store path base name is always a valid `Filename`: it
 * starts with the hash part, so it is not empty, `.` or `..`, and the
 * characters allowed in it don't include `/` or NUL.
 */
static Filename checkedBaseName(std::string baseName)
{
    return Filename::fromStringAsserting(std::move(baseName));
}

static Filename checkBaseName(std::string_view baseName)
{
    if (baseName.size() < StorePath::HashLen + 1)
        throw BadStorePath("'%s' is too short to be a valid store path basename", baseName);
    for (auto c : baseName.substr(0, StorePath::HashLen))
        if (c == 'e' || c == 'o' || c == 'u' || c == 't' || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
            throw BadStorePath("store path basename '%s' contains illegal base-32 character '%s'", baseName, c);
    if (baseName[StorePath::HashLen] != '-')
        throw BadStorePath("'%s' can't name a store path because the hash part is not followed by a '-'", baseName);
    checkPathName(baseName, baseName.substr(StorePath::HashLen + 1));
    return checkedBaseName(std::string{baseName});
}

StorePath::StorePath(std::string_view _baseName)
    : baseName(checkBaseName(_baseName))
{
}

static Filename checkBaseName(const Hash & hash, std::string_view name)
{
    auto baseName = (hash.to_string(HashFormat::Nix32, false) + "-").append(std::string(name));
    checkPathName(baseName, name);
    return checkedBaseName(std::move(baseName));
}

StorePath::StorePath(const Hash & hash, std::string_view _name)
    : baseName(checkBaseName(hash, _name))
{
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

const StorePath StorePath::dummy("ffffffffffffffffffffffffffffffff-x");

StorePath StorePath::random(std::string_view name)
{
    return StorePath(Hash::random(HashAlgorithm::SHA1), name);
}

} // namespace nix

namespace nlohmann {

nix::StorePath adl_serializer<nix::StorePath>::from_json(const json & json)
{
    return nix::StorePath{nix::getString(json)};
}

void adl_serializer<nix::StorePath>::to_json(json & json, const nix::StorePath & storePath)
{
    json = storePath.to_string();
}

} // namespace nlohmann

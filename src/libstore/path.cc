#include "store-api.hh"

namespace nix {

extern "C" {
    rust::Result<StorePath> ffi_StorePath_new(rust::StringSlice path, rust::StringSlice storeDir);
    rust::Result<StorePath> ffi_StorePath_new2(unsigned char hash[20], rust::StringSlice storeDir);
    rust::Result<StorePath> ffi_StorePath_fromBaseName(rust::StringSlice baseName);
    rust::String ffi_StorePath_to_string(const StorePath & _this);
    StorePath ffi_StorePath_clone(const StorePath & _this);
    rust::StringSlice ffi_StorePath_name(const StorePath & _this);
}

StorePath StorePath::make(std::string_view path, std::string_view storeDir)
{
    return ffi_StorePath_new((rust::StringSlice) path, (rust::StringSlice) storeDir).unwrap();
}

StorePath StorePath::make(unsigned char hash[20], std::string_view name)
{
    return ffi_StorePath_new2(hash, (rust::StringSlice) name).unwrap();
}

StorePath StorePath::fromBaseName(std::string_view baseName)
{
    return ffi_StorePath_fromBaseName((rust::StringSlice) baseName).unwrap();
}

rust::String StorePath::to_string() const
{
    return ffi_StorePath_to_string(*this);
}

StorePath StorePath::clone() const
{
    return ffi_StorePath_clone(*this);
}

bool StorePath::isDerivation() const
{
    return hasSuffix(name(), drvExtension);
}

std::string_view StorePath::name() const
{
    return ffi_StorePath_name(*this);
}

StorePath Store::parseStorePath(std::string_view path) const
{
    return StorePath::make(path, storeDir);
}


StorePathSet Store::parseStorePathSet(const PathSet & paths) const
{
    StorePathSet res;
    for (auto & i : paths) res.insert(parseStorePath(i));
    return res;
}

std::string Store::printStorePath(const StorePath & path) const
{
    auto s = storeDir + "/";
    s += (std::string_view) path.to_string();
    return s;
}

PathSet Store::printStorePathSet(const StorePathSet & paths) const
{
    PathSet res;
    for (auto & i : paths) res.insert(printStorePath(i));
    return res;
}

StorePathSet cloneStorePathSet(const StorePathSet & paths)
{
    StorePathSet res;
    for (auto & p : paths)
        res.insert(p.clone());
    return res;
}

StorePathSet storePathsToSet(const StorePaths & paths)
{
    StorePathSet res;
    for (auto & p : paths)
        res.insert(p.clone());
    return res;
}

StorePathSet singleton(const StorePath & path)
{
    StorePathSet res;
    res.insert(path.clone());
    return res;
}

}

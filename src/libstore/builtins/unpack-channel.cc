#include "nix/store/builtins.hh"
#include "nix/util/tarfile.hh"

namespace nix {

namespace fs {
using namespace std::filesystem;
}

void builtinUnpackChannel(const BasicDerivation & drv, const std::map<std::string, Path> & outputs)
{
    auto getAttr = [&](const std::string & name) -> const std::string & {
        auto i = drv.env.find(name);
        if (i == drv.env.end())
            throw Error("attribute '%s' missing", name);
        return i->second;
    };

    fs::path out{outputs.at("out")};
    auto & channelName = getAttr("channelName");
    auto & src = getAttr("src");

    if (fs::path{channelName}.filename().string() != channelName) {
        throw Error("channelName is not allowed to contain filesystem separators, got %1%", channelName);
    }

    createDirs(out);

    unpackTarfile(src, out);

    size_t fileCount;
    std::string fileName;
    auto entries = DirectoryIterator{out};
    fileName = entries->path().string();
    fileCount = std::distance(entries.begin(), entries.end());

    if (fileCount != 1)
        throw Error("channel tarball '%s' contains more than one file", src);

    auto target = out / channelName;
    try {
        fs::rename(fileName, target);
    } catch (fs::filesystem_error &) {
        throw SysError("failed to rename %1% to %2%", fileName, target.string());
    }
}

} // namespace nix

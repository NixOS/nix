#include "builtins.hh"
#include "tarfile.hh"

namespace nix {

void builtinUnpackChannel(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs)
{
    auto getAttr = [&](const std::string & name) {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    std::filesystem::path out(outputs.at("out"));
    std::filesystem::path channelName(getAttr("channelName"));
    auto src = getAttr("src");

    if (channelName.filename() != channelName) {
        throw Error("channelName is not allowed to contain filesystem seperators, got %1%", channelName);
    }

    createDirs(out);

    unpackTarfile(src, out);

    size_t fileCount;
    std::string fileName;
    try {
        auto entries = std::filesystem::directory_iterator{out};
        fileName = entries->path().string();
        fileCount = std::distance(std::filesystem::begin(entries), std::filesystem::end(entries));
    } catch (std::filesystem::filesystem_error &e) {
        throw SysError("failed to read directory %1%", out);
    }


    if (fileCount != 1)
        throw Error("channel tarball '%s' contains more than one file", src);
    std::filesystem::path target(out / channelName);
    try {
        std::filesystem::rename(fileName, target);
    } catch (std::filesystem::filesystem_error &e) {
        throw SysError("failed to rename %1% to %2%", fileName, target);
    }
}

}

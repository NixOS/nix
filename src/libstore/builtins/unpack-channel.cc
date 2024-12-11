#include "builtins.hh"
#include "tarfile.hh"

namespace nix {

namespace fs { using namespace std::filesystem; }

void builtinUnpackChannel(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs)
{
    auto getAttr = [&](const std::string & name) -> const std::string & {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    fs::path out{outputs.at("out")};
    auto & channelName = getAttr("channelName");
    auto & src = getAttr("src");

    if (fs::path{channelName}.filename().string() != channelName) {
        throw Error("channelName is not allowed to contain filesystem separators, got %1%", channelName);
    }

    try {
        fs::create_directories(out);
    } catch (fs::filesystem_error &) {
        throw SysError("creating directory '%1%'", out.string());
    }

    unpackTarfile(src, out);

    size_t fileCount;
    std::string fileName;
    try {
        auto entries = fs::directory_iterator{out};
        fileName = entries->path().string();
        fileCount = std::distance(fs::begin(entries), fs::end(entries));
    } catch (fs::filesystem_error &) {
        throw SysError("failed to read directory %1%", out.string());
    }

    if (fileCount != 1)
        throw Error("channel tarball '%s' contains more than one file", src);

    auto target = out / channelName;
    try {
        fs::rename(fileName, target);
    } catch (fs::filesystem_error &) {
        throw SysError("failed to rename %1% to %2%", fileName, target.string());
    }
}

}

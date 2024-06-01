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

    auto out = outputs.at("out");
    auto channelName = getAttr("channelName");
    auto src = getAttr("src");

    createDirs(out);

    unpackTarfile(src, out);

    auto entries = std::filesystem::directory_iterator{out};
    auto fileName = entries->path().string();
    auto fileCount = std::distance(std::filesystem::begin(entries), std::filesystem::end(entries));

    if (fileCount != 1)
        throw Error("channel tarball '%s' contains more than one file", src);
    std::filesystem::rename(fileName, (out + "/" + channelName));
}

}

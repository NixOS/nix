#include "builtins.hh"
#include "tarfile.hh"

namespace nix {

void builtinUnpackChannel(const BasicDerivation & drv)
{
    auto getAttr = [&](const std::string & name) {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    Path out = getAttr("out");
    auto channelName = getAttr("channelName");
    auto src = getAttr("src");

    createDirs(out);

    unpackTarfile(src, out);

    auto entries = readDirectory(out);
    if (entries.size() != 1)
        throw Error("channel tarball '%s' contains more than one file", src);
    if (rename((out + "/" + entries[0].name).c_str(), (out + "/" + channelName).c_str()) == -1)
        throw SysError("renaming channel directory");
}

}

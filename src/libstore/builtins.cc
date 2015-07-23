#include "builtins.hh"
#include "download.hh"

namespace nix {

void builtinFetchurl(const BasicDerivation & drv)
{
    auto url = drv.env.find("url");
    if (url == drv.env.end()) throw Error("attribute ‘url’ missing");
    printMsg(lvlInfo, format("downloading ‘%1%’...") % url->second);
    auto data = downloadFile(url->second); // FIXME: show progress

    auto out = drv.env.find("out");
    if (out == drv.env.end()) throw Error("attribute ‘url’ missing");
    writeFile(out->second, data.data);

    auto executable = drv.env.find("executable");
    if (executable != drv.env.end() && executable->second == "1") {
        if (chmod(out->second.c_str(), 0755) == -1)
            throw SysError(format("making ‘%1%’ executable") % out->second);
    }
}

}

#include "builtins.hh"
#include "download.hh"
#include "store-api.hh"
#include "archive.hh"
#include "compression.hh"

namespace nix {

void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData)
{
    /* Make the host's netrc data available. Too bad curl requires
       this to be stored in a file. It would be nice if we could just
       pass a pointer to the data. */
    if (netrcData != "") {
        settings.netrcFile = "netrc";
        writeFile(settings.netrcFile, netrcData, 0600);
    }

    auto getAttr = [&](const string & name) {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error(format("attribute ‘%s’ missing") % name);
        return i->second;
    };

    auto fetch = [&](const string & url) {
        /* No need to do TLS verification, because we check the hash of
           the result anyway. */
        DownloadRequest request(url);
        request.verifyTLS = false;

        /* Show a progress indicator, even though stderr is not a tty. */
        request.showProgress = DownloadRequest::yes;

        /* Note: have to use a fresh downloader here because we're in
           a forked process. */
        auto data = makeDownloader()->download(request);
        assert(data.data);

        return data.data;
    };

    std::shared_ptr<std::string> data;

    try {
        if (getAttr("outputHashMode") == "flat")
            data = fetch("http://tarballs.nixos.org/" + getAttr("outputHashAlgo") + "/" + getAttr("outputHash"));
    } catch (Error & e) {
        debug(e.what());
    }

    if (!data) data = fetch(getAttr("url"));

    Path storePath = getAttr("out");

    auto unpack = drv.env.find("unpack");
    if (unpack != drv.env.end() && unpack->second == "1") {
        if (string(*data, 0, 6) == string("\xfd" "7zXZ\0", 6))
            data = decompress("xz", *data);
        StringSource source(*data);
        restorePath(storePath, source);
    } else
        writeFile(storePath, *data);

    auto executable = drv.env.find("executable");
    if (executable != drv.env.end() && executable->second == "1") {
        if (chmod(storePath.c_str(), 0755) == -1)
            throw SysError(format("making ‘%1%’ executable") % storePath);
    }
}

}

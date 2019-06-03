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
        if (i == drv.env.end()) throw Error(format("attribute '%s' missing") % name);
        return i->second;
    };

    Path storePath = getAttr("out");
    auto mainUrl = getAttr("url");
    bool unpack = get(drv.env, "unpack", "") == "1";

    /* Note: have to use a fresh downloader here because we're in
       a forked process. */
    auto downloader = makeDownloader();

    auto fetch = [&](const std::string & url) {

        auto source = sinkToSource([&](Sink & sink) {

            /* No need to do TLS verification, because we check the hash of
               the result anyway. */
            DownloadRequest request(url);
            request.verifyTLS = false;
            request.decompress = false;

            auto decompressor = makeDecompressionSink(
                unpack && hasSuffix(mainUrl, ".xz") ? "xz" : "none", sink);
            downloader->download(std::move(request), *decompressor);
            decompressor->finish();
        });

        if (unpack)
            restorePath(storePath, *source);
        else
            writeFile(storePath, *source);

        auto executable = drv.env.find("executable");
        if (executable != drv.env.end() && executable->second == "1") {
            if (chmod(storePath.c_str(), 0755) == -1)
                throw SysError(format("making '%1%' executable") % storePath);
        }
    };

    /* Try the hashed mirrors first. */
    if (getAttr("outputHashMode") == "flat")
        for (auto hashedMirror : settings.hashedMirrors.get())
            try {
                if (!hasSuffix(hashedMirror, "/")) hashedMirror += '/';
                auto ht = parseHashType(getAttr("outputHashAlgo"));
                auto h = Hash(getAttr("outputHash"), ht);
                fetch(hashedMirror + printHashType(h.type) + "/" + h.to_string(Base16, false));
                return;
            } catch (Error & e) {
                debug(e.what());
            }

    /* Otherwise try the specified URL. */
    fetch(mainUrl);
}

}

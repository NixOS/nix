#include "builtins.hh"
#include "filetransfer.hh"
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
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    Path storePath = getAttr("out");
    auto mainUrl = getAttr("url");
    bool unpack = get(drv.env, "unpack").value_or("") == "1";

    /* Note: have to use a fresh fileTransfer here because we're in
       a forked process. */
    auto fileTransfer = makeFileTransfer();

    auto fetch = [&](const std::string & url) {

        auto source = sinkToSource([&](Sink & sink) {

            /* No need to do TLS verification, because we check the hash of
               the result anyway. */
            FileTransferRequest request(url);
            request.verifyTLS = false;
            request.decompress = false;

            auto decompressor = makeDecompressionSink(
                unpack && hasSuffix(mainUrl, ".xz") ? "xz" : "none", sink);
            fileTransfer->download(std::move(request), *decompressor);
            decompressor->finish();
        });

        if (unpack)
            restorePath(storePath, *source);
        else
            writeFile(storePath, *source);

        auto executable = drv.env.find("executable");
        if (executable != drv.env.end() && executable->second == "1") {
            if (chmod(storePath.c_str(), 0755) == -1)
                throw SysError("making '%1%' executable", storePath);
        }
    };

    /* We always have one output, and if it's a fixed-output derivation (as
       checked below) it must be the only output */
    auto & output = drv.outputs.begin()->second;

    /* Try the hashed mirrors first. */
    if (auto hash = std::get_if<DerivationOutputFixed>(&output.output)) {
        if (hash->hash.method == FileIngestionMethod::Flat) {
            for (auto hashedMirror : settings.hashedMirrors.get()) {
                try {
                    if (!hasSuffix(hashedMirror, "/")) hashedMirror += '/';
                    fetch(hashedMirror + printHashType(*hash->hash.hash.type) + "/" + hash->hash.hash.to_string(Base16, false));
                    return;
                } catch (Error & e) {
                    debug(e.what());
                }
            }
        }
    }

    /* Otherwise try the specified URL. */
    fetch(mainUrl);
}

}

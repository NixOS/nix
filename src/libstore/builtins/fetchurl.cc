#include "builtins.hh"
#include "filetransfer.hh"
#include "store-api.hh"
#include "archive.hh"
#include "compression.hh"

namespace nix {

void builtinFetchurl(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs,
    const std::string & netrcData)
{
    /* Make the host's netrc data available. Too bad curl requires
       this to be stored in a file. It would be nice if we could just
       pass a pointer to the data. */
    if (netrcData != "") {
        settings.netrcFile = "netrc";
        writeFile(settings.netrcFile, netrcData, 0600);
    }

    auto out = get(drv.outputs, "out");
    if (!out)
        throw Error("'builtin:fetchurl' requires an 'out' output");

    if (!(drv.type().isFixed() || drv.type().isImpure()))
        throw Error("'builtin:fetchurl' must be a fixed-output or impure derivation");

    auto storePath = outputs.at("out");
    auto mainUrl = drv.env.at("url");
    bool unpack = getOr(drv.env, "unpack", "") == "1";

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

    /* Try the hashed mirrors first. */
    auto dof = std::get_if<DerivationOutput::CAFixed>(&out->raw);
    if (dof && dof->ca.method.getFileIngestionMethod() == FileIngestionMethod::Flat)
        for (auto hashedMirror : settings.hashedMirrors.get())
            try {
                if (!hasSuffix(hashedMirror, "/")) hashedMirror += '/';
                fetch(hashedMirror + printHashAlgo(dof->ca.hash.algo) + "/" + dof->ca.hash.to_string(HashFormat::Base16, false));
                return;
            } catch (Error & e) {
                debug(e.what());
            }

    /* Otherwise try the specified URL. */
    fetch(mainUrl);
}

}

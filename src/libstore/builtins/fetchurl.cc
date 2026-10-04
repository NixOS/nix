#include "nix/store/builtins.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/store-api.hh"
#include "nix/util/archive.hh"
#include "nix/util/compression.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"

namespace nix {

namespace {

/**
 * The netrc that the parent process copied into the sandbox, served to this
 * fetch's transfers.
 *
 * A build has no broker to lease from, so the lease here is plain ownership:
 * the file is written once, shared by every transfer of the fetch, and
 * unlinked once the last of them lets go of it.
 */
class SandboxNetrcFile : public SecretFile
{
public:
    SandboxNetrcFile(std::filesystem::path path, std::string_view data)
        : filePath(std::move(path))
    {
        writeFile(filePath, data, 0600);
    }

    ~SandboxNetrcFile() override
    {
        try {
            deletePath(filePath);
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }

    const std::filesystem::path & path() const noexcept override
    {
        return filePath;
    }

private:
    std::filesystem::path filePath;
};

class SandboxNetrcResolver : public SecretResolver
{
public:
    SandboxNetrcResolver(std::filesystem::path path, std::string_view data)
        : netrc(make_ref<SandboxNetrcFile>(std::move(path), data))
    {
    }

    std::optional<ResolvedSecret> resolve(const SecretRequest & request) override
    {
        if (request.name != "netrc")
            return std::nullopt;
        if (request.representation != SecretRepresentation::MaterialisedFile)
            throw Error("the sandboxed netrc can only be served as a file");
        return ResolvedSecret{.value = netrc};
    }

private:
    ref<SecretFile> netrc;
};

} // namespace

static void builtinFetchurl(const BuiltinBuilderContext & ctx)
{
    /* Make the host's netrc data available to this fetch's transfers. Too
       bad curl requires this to be stored in a file. It would be nice if we
       could just pass a pointer to the data. */
    FileTransferContext transferContext;
    if (ctx.netrcData)
        transferContext.secretResolver =
            std::make_shared<SandboxNetrcResolver>(ctx.tmpDirInSandbox / "netrc", *ctx.netrcData);

    /* Same for the CA bundle: the sandbox has no certificates of its own,
       so every request below is pointed at the parent's copy. */
    auto caFilePath = ctx.tmpDirInSandbox / "ca-certificates.crt";
    writeFile(caFilePath, ctx.caFileData, 0600);

    auto out = get(ctx.drv.outputs, "out");
    if (!out)
        throw Error("'builtin:fetchurl' requires an 'out' output");

    if (!(type(ctx.drv).isFixed() || type(ctx.drv).isImpure()))
        throw Error("'builtin:fetchurl' must be a fixed-output or impure derivation");

    auto storePath = ctx.outputs.at("out");
    auto mainUrl = ctx.drv.env.at("url");
    bool unpack = getOr(ctx.drv.env, "unpack", "") == "1";

    /* Note: have to use a fresh fileTransfer here because we're in
       a forked process. */
    debug("[pid=%d] builtin:fetchurl creating fresh FileTransfer instance", getpid());
    auto fileTransfer = makeFileTransfer();

    auto fetch = [&](const std::string & url) {
        auto source = sinkToSource([&](Sink & sink) {
            FileTransferRequest request(VerbatimURL{url});
            request.decompress = false;
            request.caFile = caFilePath;

#if NIX_WITH_AWS_AUTH
            // Use pre-resolved credentials if available
            if (ctx.awsCredentials && request.uri.scheme() == "s3") {
                debug("[pid=%d] Using pre-resolved AWS credentials from parent process", getpid());
                request.usernameAuth = UsernameAuth{
                    .username = ctx.awsCredentials->accessKeyId,
                    .password = ctx.awsCredentials->secretAccessKey,
                };
                request.preResolvedAwsSessionToken = ctx.awsCredentials->sessionToken;
            }
#endif

            auto decompressor = makeDecompressionSink(
                unpack && hasSuffix(mainUrl, ".xz") ? CompressionAlgo::xz : CompressionAlgo::none, sink);
            fileTransfer->download(transferContext, std::move(request), *decompressor);
            decompressor->finish();
        });

        if (unpack)
            restorePath(storePath, *source);
        else
            writeFile(storePath, *source);

        auto executable = ctx.drv.env.find("executable");
        if (executable != ctx.drv.env.end() && executable->second == "1") {
            chmod(storePath, 0755);
        }
    };

    /* Try the hashed mirrors first. */
    auto dof = std::get_if<DerivationOutput::CAFixed>(&out->raw);
    if (dof && dof->ca.method.getFileIngestionMethod() == FileIngestionMethod::Flat)
        for (auto hashedMirror : ctx.hashedMirrors)
            try {
                if (!hasSuffix(hashedMirror, "/"))
                    hashedMirror += '/';
                fetch(
                    hashedMirror + printHashAlgo(dof->ca.hash.algo) + "/"
                    + dof->ca.hash.to_string(HashFormat::Base16, false));
                return;
            } catch (Error & e) {
                debug(e.what());
            }

    /* Otherwise try the specified URL. */
    fetch(mainUrl);
}

static RegisterBuiltinBuilder registerFetchurl("fetchurl", builtinFetchurl);

} // namespace nix

#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/config.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif
#if NIX_WITH_GCS_AUTH
#  include "nix/store/gcp-creds.hh"
#endif

namespace nix {

struct BuiltinBuilderContext
{
    const BasicDerivation & drv;
    std::map<std::string, std::string> outputs;
    std::string netrcData;
    std::string caFileData;
    Strings hashedMirrors;
    std::filesystem::path tmpDirInSandbox;

#if NIX_WITH_AWS_AUTH
    /**
     * Pre-resolved AWS credentials for S3 URLs in builtin:fetchurl.
     * When present, these should be used instead of creating new credential providers.
     */
    std::optional<AwsCredentials> awsCredentials;
#endif

#if NIX_WITH_GCS_AUTH
    /**
     * Pre-resolved GCP OAuth2 bearer token for gs:// URLs in builtin:fetchurl.
     * The sandboxed child has no access to the ADC file or metadata server.
     * The parent resolves the token before forking.
     */
    std::optional<std::string> gcpAccessToken;
#endif
};

using BuiltinBuilder = fun<void(const BuiltinBuilderContext &)>;

struct RegisterBuiltinBuilder
{
    typedef std::map<std::string, BuiltinBuilder> BuiltinBuilders;

    static BuiltinBuilders & builtinBuilders();

    RegisterBuiltinBuilder(const std::string & name, BuiltinBuilder && builder)
    {
        builtinBuilders().insert_or_assign(name, std::move(builder));
    }
};

} // namespace nix

#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/config.hh"

#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif

namespace nix {

struct BuiltinBuilderContext
{
    const BasicDerivation & drv;
    std::map<std::string, Path> outputs;
    std::string netrcData;
    std::string caFileData;
    Path tmpDirInSandbox;

#if NIX_WITH_AWS_AUTH
    /**
     * Pre-resolved AWS credentials for S3 URLs in builtin:fetchurl.
     * When present, these should be used instead of creating new credential providers.
     */
    std::optional<AwsCredentials> awsCredentials;
#endif
};

using BuiltinBuilder = std::function<void(const BuiltinBuilderContext &)>;

struct RegisterBuiltinBuilder
{
    typedef std::map<std::string, BuiltinBuilder> BuiltinBuilders;

    static BuiltinBuilders & builtinBuilders()
    {
        static BuiltinBuilders builders;
        return builders;
    }

    RegisterBuiltinBuilder(const std::string & name, BuiltinBuilder && fun)
    {
        builtinBuilders().insert_or_assign(name, std::move(fun));
    }
};

} // namespace nix

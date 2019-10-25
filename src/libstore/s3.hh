#pragma once

#if ENABLE_S3

#include "ref.hh"

namespace Aws { namespace Client { class ClientConfiguration; } }
namespace Aws { namespace S3 { class S3Client; } }

namespace nix {

struct S3Helper
{
    ref<Aws::Client::ClientConfiguration> config;
    ref<Aws::S3::S3Client> client;

    S3Helper(const std::string & profile, const std::string & region, const std::string & scheme, const std::string & endpoint);

    ref<Aws::Client::ClientConfiguration> makeConfig(const std::string & region, const std::string & scheme, const std::string & endpoint);

    struct DownloadResult
    {
        std::shared_ptr<std::string> data;
        unsigned int durationMs;
    };

    DownloadResult getObject(
        const std::string & bucketName, const std::string & key);
};

}

#endif

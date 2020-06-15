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

    S3Helper(std::string_view profile, std::string_view region, std::string_view scheme, std::string_view endpoint);

    ref<Aws::Client::ClientConfiguration> makeConfig(std::string_view region, std::string_view scheme, std::string_view endpoint);

    struct FileTransferResult
    {
        std::shared_ptr<std::string> data;
        unsigned int durationMs;
    };

    FileTransferResult getObject(
        std::string_view bucketName, std::string_view key);
};

}

#endif

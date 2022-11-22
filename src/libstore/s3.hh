#pragma once

#if ENABLE_S3

#include "ref.hh"
#include "serialise.hh"

#include <optional>
#include <string>

namespace Aws { namespace Client { class ClientConfiguration; } }
namespace Aws { namespace S3 { class S3Client; } }

namespace nix {

struct S3Helper
{
    ref<Aws::Client::ClientConfiguration> config;
    ref<Aws::S3::S3Client> client;

    S3Helper(const std::string & profile, const std::string & region, const std::string & scheme, const std::string & endpoint);

    ref<Aws::Client::ClientConfiguration> makeConfig(const std::string & region, const std::string & scheme, const std::string & endpoint);

    struct FileTransferResult
    {
        std::optional<std::string> data;
        unsigned int durationMs;
    };

    FileTransferResult getObject(
        const std::string & bucketName, const std::string & key);

    struct StreamResult
    {
        std::streamsize size = 0;
        unsigned int durationMs = 0;
        bool streamSuccessful = false;
    };

    StreamResult getObjectStreamed(
        const std::string & bucketName, const std::string & key, Sink & sink, size_t bufferSize);

    size_t getObjectSize(const std::string & bucketName, const std::string & key);
};

}

#endif

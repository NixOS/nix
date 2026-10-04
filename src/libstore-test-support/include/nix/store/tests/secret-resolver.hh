#pragma once
///@file

#include "nix/store/secret-resolver.hh"

#include <functional>
#include <filesystem>
#include <utility>
#include <vector>

namespace nix::testing {

class CallbackSecretFile : public SecretFile
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    void anchor() override;

public:
    CallbackSecretFile(std::filesystem::path path, std::function<void()> onDestroy = {})
        : filePath(std::move(path))
        , onDestroy(std::move(onDestroy))
    {
    }

    ~CallbackSecretFile() override
    {
        if (onDestroy)
            onDestroy();
    }

    const std::filesystem::path & path() const noexcept override
    {
        return filePath;
    }

private:
    std::filesystem::path filePath;
    std::function<void()> onDestroy;
};

/**
 * Records what was asked for and answers from a callback.
 *
 * Not thread-safe, unlike a real resolver: tests drive it from one thread
 * and want the request log to stay in call order.
 */
class CallbackSecretResolver : public SecretResolver
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    void anchor() override;

public:
    using Callback = std::function<std::optional<ResolvedSecret>(const SecretRequest &)>;

    explicit CallbackSecretResolver(Callback callback)
        : callback(std::move(callback))
    {
    }

    std::optional<ResolvedSecret> resolve(const SecretRequest & request) override
    {
        requests.push_back(request);
        return callback(request);
    }

    std::vector<SecretRequest> requests;

private:
    Callback callback;
};

} // namespace nix::testing

#pragma once

#include "binary-cache-store.hh"

#include <atomic>

namespace nix {

class S3BinaryCacheStore : public BinaryCacheStore
{
protected:

    S3BinaryCacheStore(std::shared_ptr<Store> localStore,
        const StoreParams & params)
        : BinaryCacheStore(localStore, params)
    { }

public:

    struct Stats
    {
        std::atomic<uint64_t> put{0};
        std::atomic<uint64_t> putBytes{0};
        std::atomic<uint64_t> putTimeMs{0};
        std::atomic<uint64_t> get{0};
        std::atomic<uint64_t> getBytes{0};
        std::atomic<uint64_t> getTimeMs{0};
        std::atomic<uint64_t> head{0};
    };

    const Stats & getS3Stats();
};

}

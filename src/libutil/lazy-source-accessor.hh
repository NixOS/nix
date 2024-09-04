#pragma once

#include "source-accessor.hh"
#include "sync.hh"

#include <variant>

namespace nix {

/**
 * A wrapper `SourceAccessor` that lazily constructs an underlying
 * `SourceAccessor`.
 */
struct LazySourceAccessor : SourceAccessor
{
    using Fun = std::function<ref<SourceAccessor>()>;

    Sync<std::variant<ref<SourceAccessor>, Fun>> next_;

    LazySourceAccessor(Fun next)
        : next_{{std::move(next)}}
    {
    }

    ref<SourceAccessor> getNext()
    {
        auto next(next_.lock());
        if (auto accessor = std::get_if<ref<SourceAccessor>>(&*next))
            return *accessor;
        else {
            auto fun = std::get<Fun>(*next);
            auto acc = fun();
            *next = acc;
            return acc;
        }
        abort();
    }

    std::string readFile(const CanonPath & path) override
    {
        return getNext()->readFile(path);
    }

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        return getNext()->readFile(path, sink, sizeCallback);
    }

    bool pathExists(const CanonPath & path) override
    {
        return getNext()->pathExists(path);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        return getNext()->maybeLstat(path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return getNext()->readDirectory(path);
    }

    std::string readLink(const CanonPath & path) override
    {
        return getNext()->readLink(path);
    }
};

}

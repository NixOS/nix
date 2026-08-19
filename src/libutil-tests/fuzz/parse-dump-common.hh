#include "nix/util/archive.hh"

namespace nix {

int fuzzParseDumpCommon(std::string_view s, bool acceptInvalid)
{
    struct DrainingFileSystemObjectSink : NullFileSystemObjectSink
    {
        void createRegularFile(const CanonPath &, fun<void(CreateRegularFileSink &)> func) override
        {
            struct : CreateRegularFileSink
            {
                void operator()(std::string_view) override {}

                void isExecutable() override {}
            } sink;

            func(sink);
        }
    };

    struct ParseResult
    {
        bool accepted;
        size_t consumed;
    };

    auto parse = [&](FileSystemObjectSink & sink) {
        nix::StringSource source{s};
        try {
            nix::parseDump(sink, source);
            return ParseResult{.accepted = true, .consumed = source.pos};
        } catch (const nix::Error &) {
            return ParseResult{.accepted = false, .consumed = source.pos};
        }
    };

    NullFileSystemObjectSink skippingSink;
    auto skipping = parse(skippingSink);

    DrainingFileSystemObjectSink drainingSink;
    auto draining = parse(drainingSink);

    if (skipping.accepted != draining.accepted || (skipping.accepted && skipping.consumed != draining.consumed))
        __builtin_trap();

    if (!skipping.accepted)
        return acceptInvalid ? 0 : -1;

    /* TODO: Also check that it round-trips with dumpPath. Need to work around
       overly large allocation issues with MemorySourceAccessor probably. Also
       fuzz with more sink types. */
    return 0;
}

} // namespace nix

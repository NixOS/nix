#include "command.hh"
#include "store-api.hh"
#include "common-args.hh"
#include "compression.hh"

using namespace nix;

namespace rust {

// Depending on the internal representation of Rust slices is slightly
// evil...
template<typename T> struct Slice
{
    T * ptr;
    size_t size;

    Slice(T * ptr, size_t size) : ptr(ptr), size(size)
    {
        assert(ptr);
    }
};

struct StringSlice : Slice<char>
{
    StringSlice(const std::string & s): Slice((char *) s.data(), s.size()) {}
};

struct Source
{
    size_t (*fun)(void * source_this, rust::Slice<uint8_t> data);
    nix::Source * _this;

    Source(nix::Source & _this)
        : fun(sourceWrapper), _this(&_this)
    {}

    // FIXME: how to propagate exceptions?
    static size_t sourceWrapper(void * _this, rust::Slice<uint8_t> data)
    {
        auto n = ((nix::Source *) _this)->read(data.ptr, data.size);
        return n;
    }
};

}

extern "C" {
    bool unpack_tarfile(rust::Source source, rust::StringSlice dest_dir);
}

struct CmdTest : StoreCommand
{
    CmdTest()
    {
    }

    std::string name() override
    {
        return "test";
    }

    std::string description() override
    {
        return "bla bla";
    }

    void run(ref<Store> store) override
    {
        auto source = sinkToSource([&](Sink & sink) {
            auto decompressor = makeDecompressionSink("bzip2", sink);
            readFile("./nix-2.2.tar.bz2", *decompressor);
            decompressor->finish();
        });

        std::string destDir = "./dest";

        deletePath(destDir);

        unpack_tarfile(*source, destDir);
    }
};

static RegisterCommand r(make_ref<CmdTest>());

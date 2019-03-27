#include "command.hh"
#include "store-api.hh"
#include "common-args.hh"

using namespace nix;

namespace rust {

// Depending on the internal representation of Rust slices is slightly
// evil...
template<typename T> struct Slice
{
    const T * ptr;
    size_t size;

    Slice(const T * ptr, size_t size) : ptr(ptr), size(size)
    {
        assert(ptr);
    }
};

struct StringSlice : Slice<char>
{
    StringSlice(const std::string & s): Slice(s.data(), s.size()) { }
};

}

extern "C" {
    bool unpack_tarfile(rust::Slice<uint8_t> data, rust::StringSlice dest_dir);
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
        auto data = readFile("./nix-2.2.tar");

        std::string destDir = "./dest";

        deletePath(destDir);

        unpack_tarfile({(uint8_t*) data.data(), data.size()}, destDir);
    }
};

static RegisterCommand r(make_ref<CmdTest>());

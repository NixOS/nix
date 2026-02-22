#include "nix/store/builtins.hh"
#include "nix/util/tarfile.hh"

namespace nix {

static void builtinUnpackChannel(const BuiltinBuilderContext & ctx)
{
    auto getAttr = [&](const std::string & name) -> const std::string & {
        auto i = ctx.drv.env.find(name);
        if (i == ctx.drv.env.end())
            throw Error("attribute '%s' missing", name);
        return i->second;
    };

    std::filesystem::path out{ctx.outputs.at("out")};
    auto & channelName = getAttr("channelName");
    auto & src = getAttr("src");

    if (std::filesystem::path{channelName}.filename().string() != channelName) {
        throw Error("channelName is not allowed to contain filesystem separators, got %1%", channelName);
    }

    createDirs(out);

    unpackTarfile(src, out);

    size_t fileCount;
    std::string fileName;
    auto entries = DirectoryIterator{out};
    if (entries == DirectoryIterator{})
        throw Error("channel tarball '%s' is empty", src);
    fileName = entries->path().string();
    fileCount = std::distance(entries.begin(), entries.end());

    if (fileCount != 1)
        throw Error("channel tarball '%s' contains more than one file", src);

    auto target = out / channelName;
    try {
        std::filesystem::rename(fileName, target);
    } catch (std::filesystem::filesystem_error & e) {
        throw SystemError(e.code(), "failed to rename %1% to %2%", fileName, target.string());
    }
}

static RegisterBuiltinBuilder registerUnpackChannel("unpack-channel", builtinUnpackChannel);

} // namespace nix

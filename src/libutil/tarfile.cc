#include "rust-ffi.hh"
#include "compression.hh"

extern "C" {
    rust::Result<std::tuple<>> *
    unpack_tarfile(rust::Source source, rust::StringSlice dest_dir);
}

namespace nix {

void unpackTarfile(Source & source, const Path & destDir)
{
    rust::Source source2(source);
    rust::CBox(unpack_tarfile(source2, destDir))->unwrap();
}

void unpackTarfile(const Path & tarFile, const Path & destDir,
    std::optional<std::string> baseName)
{
    if (!baseName) baseName = std::string(baseNameOf(tarFile));

    auto source = sinkToSource([&](Sink & sink) {
        // FIXME: look at first few bytes to determine compression type.
        auto decompressor =
            // FIXME: add .gz support
            hasSuffix(*baseName, ".bz2") ? makeDecompressionSink("bzip2", sink) :
            hasSuffix(*baseName, ".xz") ? makeDecompressionSink("xz", sink) :
            makeDecompressionSink("none", sink);
        readFile(tarFile, *decompressor);
        decompressor->finish();
    });

    unpackTarfile(*source, destDir);
}

}

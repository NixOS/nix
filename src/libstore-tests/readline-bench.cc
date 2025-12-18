#include <benchmark/benchmark.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"

#ifndef _WIN32

#  include <fcntl.h>
#  include <unistd.h>

using namespace nix;

static void BM_ReadLineFile(benchmark::State & state)
{
    const int lineCount = state.range(0);
    const std::string line = std::string(80, 'x') + "\n";
    std::string payload;
    payload.reserve(line.size() * lineCount);
    for (int i = 0; i < lineCount; ++i)
        payload += line;

    auto [file, path] = createTempFile();
    writeFull(file.get(), payload, /*allowInterrupts=*/false);
    file.close();

    for (auto _ : state) {
        state.PauseTiming();

        int flags = O_RDONLY;
#  ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#  endif
        AutoCloseFD in(open(path.c_str(), flags));
        if (!in)
            throw SysError("opening file");

        state.ResumeTiming();

        for (int i = 0; i < lineCount; ++i) {
            auto s = readLine(in.get());
            benchmark::DoNotOptimize(s);
        }
    }

    deletePath(path);

    state.SetItemsProcessed(state.iterations() * lineCount);
}

BENCHMARK(BM_ReadLineFile)->Arg(1'000)->Arg(10'000)->Arg(100'000);

#endif

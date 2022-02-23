#include "input-accessor.hh"

namespace nix {

// TODO: handle file creation / deletion.
struct PatchingInputAccessor : InputAccessor
{
    ref<InputAccessor> next;

    std::map<Path, std::vector<std::string>> patchesPerFile;

    PatchingInputAccessor(
        ref<InputAccessor> next,
        const std::vector<std::string> & patches)
        : next(next)
    {
        /* Extract the patches for each file. */
        for (auto & patch : patches) {
            std::string_view p = patch;
            std::string_view start;
            std::string_view fileName;

            auto flush = [&]()
            {
                if (start.empty()) return;
                auto contents = start.substr(0, p.data() - start.data());
                start = "";
                auto slash = fileName.find('/');
                if (slash == fileName.npos) return;
                fileName = fileName.substr(slash);
                debug("found patch for '%s'", fileName);
                patchesPerFile.emplace(Path(fileName), std::vector<std::string>())
                    .first->second.push_back(std::string(contents));
            };

            while (!p.empty()) {
                auto [line, rest] = getLine(p);

                if (hasPrefix(line, "--- ")) {
                    flush();
                    start = p;
                    fileName = line.substr(4);
                }

                if (!start.empty()) {
                    if (!(hasPrefix(line, "+++ ")
                            || hasPrefix(line, "@@")
                            || hasPrefix(line, "+")
                            || hasPrefix(line, "-")
                            || hasPrefix(line, " ")))
                    {
                        flush();
                    }
                }

                p = rest;
            }

            flush();
        }
    }

    std::string readFile(PathView path) override
    {
        auto contents = next->readFile(path);

        auto i = patchesPerFile.find((Path) path);
        if (i != patchesPerFile.end()) {
            for (auto & patch : i->second) {
                auto tempDir = createTempDir();
                AutoDelete del(tempDir);
                auto sourceFile = tempDir + "/source";
                auto rejFile = tempDir + "/source.rej";
                writeFile(sourceFile, contents);
                try {
                    contents = runProgram("patch", true, {"--quiet", sourceFile, "--output=-", "-r", rejFile}, patch);
                } catch (ExecError & e) {
                    del.cancel();
                    throw;
                }
            }
        }

        return contents;
    }

    bool pathExists(PathView path) override
    {
        return next->pathExists(path);
    }

    Stat lstat(PathView path) override
    {
        return next->lstat(path);
    }

    DirEntries readDirectory(PathView path) override
    {
        return next->readDirectory(path);
    }

    std::string readLink(PathView path) override
    {
        return next->readLink(path);
    }
};

ref<InputAccessor> makePatchingInputAccessor(
    ref<InputAccessor> next,
    const std::vector<std::string> & patches)
{
    return make_ref<PatchingInputAccessor>(next, std::move(patches));
}

}

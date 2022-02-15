#include "eval.hh"
#include "util.hh"

namespace nix {

static constexpr std::string_view marker = "/__virtual/";

Path EvalState::packPath(const SourcePath & path)
{
    // FIXME: canonPath(path) ?
    printError("PACK %s", path.path);
    assert(hasPrefix(path.path, "/"));
    inputAccessors.emplace(path.accessor->number, path.accessor);
    return std::string(marker) + std::to_string(path.accessor->number) + path.path;
}

SourcePath EvalState::unpackPath(const Path & path)
{
    printError("UNPACK %s", path);
    if (hasPrefix(path, marker)) {
        auto s = path.substr(marker.size());
        auto slash = s.find('/');
        auto n = std::stoi(s.substr(0, slash));
        auto i = inputAccessors.find(n);
        assert(i != inputAccessors.end());
        return {i->second, slash != std::string::npos ? s.substr(slash) : "/"};
    } else {
        printError("FIXME: %s", path);
        return rootPath(path);
    }
}

SourcePath EvalState::rootPath(const Path & path)
{
    printError("ROOT %s", path);
    return {rootFS, path};
}

}

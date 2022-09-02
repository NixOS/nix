#include "eval.hh"
#include "util.hh"
#include "fs-input-accessor.hh"

namespace nix {

SourcePath EvalState::rootPath(const Path & path)
{
    return {rootFS, CanonPath(path)};
}

void EvalState::registerAccessor(ref<InputAccessor> accessor)
{
    inputAccessors.emplace(accessor->number, accessor);
}

static constexpr std::string_view marker = "/__virtual__/";

std::string EvalState::encodePath(const SourcePath & path)
{
    /* For backward compatibility, return paths in the root FS
       normally. Encoding any other path is not very reproducible (due
       to /__virtual__/<N>) and we should depreceate it eventually. So
       print a warning about use of an encoded path in
       decodePath(). */
    return path.accessor == rootFS
        ? path.path.abs()
        : std::string(marker) + std::to_string(path.accessor->number) + path.path.abs();
}

SourcePath EvalState::decodePath(std::string_view s, PosIdx pos)
{
    if (!hasPrefix(s, "/"))
        throwEvalError(pos, "string '%1%' doesn't represent an absolute path", s);

    if (hasPrefix(s, marker)) {
        auto fail = [s]() {
            throw Error("cannot decode virtual path '%s'", s);
        };

        s = s.substr(marker.size());

        try {
            auto slash = s.find('/');
            if (slash == std::string::npos) fail();
            size_t number = std::stoi(std::string(s, 0, slash));
            s = s.substr(slash);

            auto accessor = inputAccessors.find(number);
            if (accessor == inputAccessors.end()) fail();

            SourcePath path {accessor->second, CanonPath(s)};

            static bool warned = false;
            warnOnce(warned, fmt("applying 'toString' to path '%s' and then accessing it is deprecated, at %s", path, positions[pos]));

            return path;
        } catch (std::invalid_argument & e) {
            fail();
            abort();
        }
    } else
        return {rootFS, CanonPath(s)};
}

}

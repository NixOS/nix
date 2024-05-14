#include "eval.hh"
#include "util.hh"
#include "fetch-to-store.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return {rootFS, std::move(path)};
}

SourcePath EvalState::rootPath(PathView path)
{
    return {rootFS, CanonPath(absPath(path))};
}

void EvalState::registerAccessor(ref<SourceAccessor> accessor)
{
    sourceAccessors.emplace(accessor->number, accessor);
}

std::string EvalState::encodePath(const SourcePath & path)
{
    /* For backward compatibility, return paths in the root FS
       normally. Encoding any other path is not very reproducible (due
       to /nix/store/virtual000...<N>) and we should deprecate it
       eventually. So print a warning about use of an encoded path in
       decodePath(). */
    return path.accessor == ref<SourceAccessor>(rootFS)
               ? path.path.abs()
               : fmt("%s%08d-source%s", virtualPathMarker, path.accessor->number, path.path.absOrEmpty());
}

SourcePath EvalState::decodePath(std::string_view s, PosIdx pos)
{
    if (!hasPrefix(s, "/"))
        error<EvalError>("string '%s' doesn't represent an absolute path", s).atPos(pos).debugThrow();

    if (hasPrefix(s, virtualPathMarker)) {
        auto fail = [s, pos, this]() { error<Abort>("cannot decode virtual path '%s'", s).atPos(pos).debugThrow(); };

        s = s.substr(virtualPathMarker.size());

        try {
            auto slash = s.find('/');
            size_t number = std::stoi(std::string(s.substr(0, slash)), nullptr, 10);
            s = slash == s.npos ? "" : s.substr(slash);

            auto accessor = sourceAccessors.find(number);
            if (accessor == sourceAccessors.end())
                fail();

            SourcePath path{accessor->second, CanonPath(s)};

            return path;
        } catch (std::invalid_argument & e) {
            fail();
            abort();
        }
    } else
        return {rootFS, CanonPath(s)};
}

std::string EvalState::prettyPrintPaths(std::string_view s)
{
    std::string res;

    size_t p = 0;

    while (true) {
        auto m = s.find(virtualPathMarker, p);
        if (m == s.npos) {
            res.append(s.substr(p));
            return res;
        }

        res.append(s.substr(p, m - p));

        auto end = s.find_first_of(" \n\r\t'\"’:", m);
        if (end == s.npos)
            end = s.size();

        try {
            auto path = decodePath(s.substr(m, end - m), noPos);
            res.append(path.to_string());
        } catch (...) {
            res.append(s.substr(m, end - m));
        }

        p = end;
    }
}

std::string EvalState::rewriteVirtualPaths(std::string_view s, std::string_view warning, PosIdx pos)
{
    std::string res;

    size_t p = 0;

    while (true) {
        auto m = s.find("lazylazy0000000000000000", p); // FIXME
        if (m == s.npos) {
            res.append(s.substr(p));
            return res;
        }

        res.append(s.substr(p, m - p));

        auto end = m + StorePath::HashLen;

        if (end > s.size()) {
            res.append(s.substr(m));
            return res;
        }

        try {
            size_t number = std::stoi(std::string(s.substr(m + 24, 8)), nullptr, 10); // FIXME

            auto accessor = sourceAccessors.find(number);
            assert(accessor != sourceAccessors.end()); // FIXME

            warn(
                std::string(warning), // FIXME: should accept a string_view
                positions[pos], accessor->second->showPath(CanonPath::root));

            res.append(fetchToStore(*store, {accessor->second}, FetchMode::DryRun).hashPart());
        } catch (...) {
            ignoreException();
            res.append(s.substr(m, end - m));
        }

        p = end;
    }
}

}

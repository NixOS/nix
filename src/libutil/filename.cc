#include "nix/util/filename.hh"

#include "nix/util/error.hh"

#include <ostream>

namespace nix {

Filename Filename::fromStringAsserting(std::string s)
{
    /* Like `OsFilename::fromPathAsserting`, as fatal as a failed
       assertion, but not compiled out with `NDEBUG`. */
    if (auto why = whyInvalid(s))
        panic(fmt("invalid file name '%s': %s", s, *why));
    return Filename{std::move(s)};
}

Filename Filename::fromStringThrowing(std::string s)
{
    if (auto why = whyInvalid(s))
        throw Error("invalid file name '%s': %s", s, *why);
    return Filename{std::move(s)};
}

void FilenameView::invalidAsserting(std::string_view s, std::string_view why)
{
    /* As fatal as a failed assertion, like
       `Filename::fromStringAsserting`. */
    panic(fmt("invalid file name '%s': %s", s, why));
}

FilenameView FilenameView::fromStringThrowing(std::string_view s)
{
    if (auto why = Filename::whyInvalid(s))
        throw Error("invalid file name '%s': %s", s, *why);
    return FilenameView{s};
}

std::ostream & operator<<(std::ostream & out, const Filename & name)
{
    return out << name.str();
}

std::ostream & operator<<(std::ostream & out, FilenameView name)
{
    return out << name.str();
}

} // namespace nix

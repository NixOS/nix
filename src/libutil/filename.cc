#include "nix/util/filename.hh"

#include "nix/util/error.hh"

#include <ostream>

namespace nix {

const char * BadFilename::describe(Problem problem)
{
    switch (problem) {
    case Problem::Empty:
        return "cannot be empty";
    case Problem::Dot:
        return "cannot be '.'";
    case Problem::DotDot:
        return "cannot be '..'";
    case Problem::Slash:
        return "cannot contain '/'";
    case Problem::ParentPath:
        return "cannot have a parent path";
    case Problem::Nul:
        return "cannot contain a NUL byte";
    }
    unreachable();
}

void BadFilename::anchor() {}

Filename Filename::fromStringAsserting(std::string s)
{
    /* Like `OsFilename::fromPathAsserting`, as fatal as a failed
       assertion, but saying why. */
    if (auto why = BadFilename::whyInvalid(s))
        panic(fmt("invalid file name '%s': %s", s, BadFilename::describe(*why)));
    return Filename{std::move(s)};
}

Filename Filename::fromStringThrowing(std::string s)
{
    if (auto why = BadFilename::whyInvalid(s))
        throw BadFilename(s, *why);
    return Filename{std::move(s)};
}

void FilenameView::dieInvalid(std::string_view s, BadFilename::Problem why)
{
    /* As fatal as a failed assertion, like
       `Filename::fromStringAsserting`. */
    panic(fmt("invalid file name '%s': %s", s, BadFilename::describe(why)));
}

FilenameView FilenameView::fromStringThrowing(std::string_view s)
{
    if (auto why = BadFilename::whyInvalid(s))
        throw BadFilename(s, *why);
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

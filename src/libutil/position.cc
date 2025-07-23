#include "nix/util/position.hh"

namespace nix {

Pos::operator std::shared_ptr<const Pos>() const
{
    return std::make_shared<const Pos>(*this);
}

std::optional<LinesOfCode> Pos::getCodeLines() const
{
    if (line == 0)
        return std::nullopt;

    if (auto source = getSource()) {
        LinesIterator lines(*source), end;
        LinesOfCode loc;

        if (line > 1)
            std::advance(lines, line - 2);
        if (lines != end && line > 1)
            loc.prevLineOfCode = *lines++;
        if (lines != end)
            loc.errLineOfCode = *lines++;
        if (lines != end)
            loc.nextLineOfCode = *lines++;

        return loc;
    }

    return std::nullopt;
}

std::optional<std::string> Pos::getSource() const
{
    return std::visit(
        overloaded{
            [](const std::monostate &) -> std::optional<std::string> { return std::nullopt; },
            [](const Pos::Stdin & s) -> std::optional<std::string> {
                // Get rid of the null terminators added by the parser.
                return std::string(s.source->c_str());
            },
            [](const Pos::String & s) -> std::optional<std::string> {
                // Get rid of the null terminators added by the parser.
                return std::string(s.source->c_str());
            },
            [](const SourcePath & path) -> std::optional<std::string> {
                try {
                    return path.readFile();
                } catch (Error &) {
                    return std::nullopt;
                }
            }},
        origin);
}

std::optional<SourcePath> Pos::getSourcePath() const
{
    if (auto * path = std::get_if<SourcePath>(&origin))
        return *path;
    return std::nullopt;
}

void Pos::print(std::ostream & out, bool showOrigin) const
{
    if (showOrigin) {
        std::visit(
            overloaded{
                [&](const std::monostate &) { out << "«none»"; },
                [&](const Pos::Stdin &) { out << "«stdin»"; },
                [&](const Pos::String & s) { out << "«string»"; },
                [&](const SourcePath & path) { out << path; }},
            origin);
        out << ":";
    }
    out << line;
    if (column > 0)
        out << ":" << column;
}

std::ostream & operator<<(std::ostream & str, const Pos & pos)
{
    pos.print(str, true);
    return str;
}

void Pos::LinesIterator::bump(bool atFirst)
{
    if (!atFirst) {
        pastEnd = input.empty();
        if (!input.empty() && input[0] == '\r')
            input.remove_prefix(1);
        if (!input.empty() && input[0] == '\n')
            input.remove_prefix(1);
    }

    // nix line endings are not only \n as eg std::getline assumes, but also
    // \r\n **and \r alone**. not treating them all the same causes error
    // reports to not match with line numbers as the parser expects them.
    auto eol = input.find_first_of("\r\n");

    if (eol > input.size())
        eol = input.size();

    curLine = input.substr(0, eol);
    input.remove_prefix(eol);
}

std::optional<std::string> Pos::getSnippetUpTo(const Pos & end) const
{
    assert(this->origin == end.origin);

    if (end.line < this->line)
        return std::nullopt;

    if (auto source = getSource()) {

        auto firstLine = LinesIterator(*source);
        for (uint32_t i = 1; i < this->line; ++i) {
            ++firstLine;
        }

        auto lastLine = LinesIterator(*source);
        for (uint32_t i = 1; i < end.line; ++i) {
            ++lastLine;
        }

        LinesIterator linesEnd;

        std::string result;
        for (auto i = firstLine; i != linesEnd; ++i) {
            auto firstColumn = i == firstLine ? (this->column ? this->column - 1 : 0) : 0;
            if (firstColumn > i->size())
                firstColumn = i->size();

            auto lastColumn = i == lastLine ? (end.column ? end.column - 1 : 0) : std::numeric_limits<int>::max();
            if (lastColumn < firstColumn)
                lastColumn = firstColumn;
            if (lastColumn > i->size())
                lastColumn = i->size();

            result += i->substr(firstColumn, lastColumn - firstColumn);

            if (i == lastLine) {
                break;
            } else {
                result += '\n';
            }
        }
        return result;
    }
    return std::nullopt;
}

} // namespace nix

#include "position.hh"

namespace nix {

Pos::Pos(const Pos * other)
{
    if (!other) {
        return;
    }
    line = other->line;
    column = other->column;
    origin = std::move(other->origin);
}

Pos::operator std::shared_ptr<Pos>() const
{
    return std::make_shared<Pos>(&*this);
}

std::optional<LinesOfCode> Pos::getCodeLines() const
{
    if (line == 0)
        return std::nullopt;

    if (auto source = getSource()) {

        std::istringstream iss(*source);
        // count the newlines.
        int count = 0;
        std::string curLine;
        int pl = line - 1;

        LinesOfCode loc;

        do {
            std::getline(iss, curLine);
            ++count;
            if (count < pl)
                ;
            else if (count == pl) {
                loc.prevLineOfCode = curLine;
            } else if (count == pl + 1) {
                loc.errLineOfCode = curLine;
            } else if (count == pl + 2) {
                loc.nextLineOfCode = curLine;
                break;
            }

            if (!iss.good())
                break;
        } while (true);

        return loc;
    }

    return std::nullopt;
}


std::optional<std::string> Pos::getSource() const
{
    return std::visit(overloaded {
        [](const Pos::none_tag &) -> std::optional<std::string> {
            return std::nullopt;
        },
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
        }
    }, origin);
}

void Pos::print(std::ostream & out, bool showOrigin) const
{
    if (showOrigin) {
        std::visit(overloaded {
            [&](const Pos::none_tag &) { out << "«none»"; },
            [&](const Pos::Stdin &) { out << "«stdin»"; },
            [&](const Pos::String & s) { out << "«string»"; },
            [&](const SourcePath & path) { out << path; }
        }, origin);
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

}

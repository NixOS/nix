#include "error.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"
#include <sstream>

namespace nix {

const std::string nativeSystem = SYSTEM;

void BaseError::addTrace(std::shared_ptr<AbstractPos> && e, hintformat hint)
{
    err.traces.push_front(Trace { .pos = std::move(e), .hint = hint });
}

// c++ std::exception descendants must have a 'const char* what()' function.
// This stringifies the error and caches it for use by what(), or similarly by msg().
const std::string & BaseError::calcWhat() const
{
    if (what_.has_value())
        return *what_;
    else {
        std::ostringstream oss;
        showErrorInfo(oss, err, loggerSettings.showTrace);
        what_ = oss.str();
        return *what_;
    }
}

std::optional<std::string> ErrorInfo::programName = std::nullopt;

std::ostream & operator <<(std::ostream & os, const hintformat & hf)
{
    return os << hf.str();
}

std::ostream & operator <<(std::ostream & str, const AbstractPos & pos)
{
    pos.print(str);
    str << ":" << pos.line;
    if (pos.column > 0)
        str << ":" << pos.column;
    return str;
}

std::optional<LinesOfCode> AbstractPos::getCodeLines() const
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

// print lines of code to the ostream, indicating the error column.
void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const AbstractPos & errPos,
    const LinesOfCode & loc)
{
    // previous line of code.
    if (loc.prevLineOfCode.has_value()) {
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
            prefix,
            (errPos.line - 1),
            *loc.prevLineOfCode);
    }

    if (loc.errLineOfCode.has_value()) {
        // line of code containing the error.
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
            prefix,
            (errPos.line),
            *loc.errLineOfCode);
        // error arrows for the column range.
        if (errPos.column > 0) {
            int start = errPos.column;
            std::string spaces;
            for (int i = 0; i < start; ++i) {
                spaces.append(" ");
            }

            std::string arrows("^");

            out << std::endl
                << fmt("%1%      |%2%" ANSI_RED "%3%" ANSI_NORMAL,
                prefix,
                spaces,
                arrows);
        }
    }

    // next line of code.
    if (loc.nextLineOfCode.has_value()) {
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
            prefix,
            (errPos.line + 1),
            *loc.nextLineOfCode);
    }
}

static std::string indent(std::string_view indentFirst, std::string_view indentRest, std::string_view s)
{
    std::string res;
    bool first = true;

    while (!s.empty()) {
        auto end = s.find('\n');
        if (!first) res += "\n";
        res += chomp(std::string(first ? indentFirst : indentRest) + std::string(s.substr(0, end)));
        first = false;
        if (end == s.npos) break;
        s = s.substr(end + 1);
    }

    return res;
}

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace)
{
    std::string prefix;
    switch (einfo.level) {
        case Verbosity::lvlError: {
            prefix = ANSI_RED "error";
            break;
        }
        case Verbosity::lvlNotice: {
            prefix = ANSI_RED "note";
            break;
        }
        case Verbosity::lvlWarn: {
            prefix = ANSI_WARNING "warning";
            break;
        }
        case Verbosity::lvlInfo: {
            prefix = ANSI_GREEN "info";
            break;
        }
        case Verbosity::lvlTalkative: {
            prefix = ANSI_GREEN "talk";
            break;
        }
        case Verbosity::lvlChatty: {
            prefix = ANSI_GREEN "chat";
            break;
        }
        case Verbosity::lvlVomit: {
            prefix = ANSI_GREEN "vomit";
            break;
        }
        case Verbosity::lvlDebug: {
            prefix = ANSI_WARNING "debug";
            break;
        }
        default:
            assert(false);
    }

    // FIXME: show the program name as part of the trace?
    if (einfo.programName && einfo.programName != ErrorInfo::programName)
        prefix += fmt(" [%s]:" ANSI_NORMAL " ", einfo.programName.value_or(""));
    else
        prefix += ":" ANSI_NORMAL " ";

    std::ostringstream oss;
    oss << einfo.msg << "\n";

    auto noSource = ANSI_ITALIC " (source not available)" ANSI_NORMAL "\n";

    if (einfo.errPos) {
        oss << "\n" << ANSI_BLUE << "at " ANSI_WARNING << *einfo.errPos << ANSI_NORMAL << ":";

        if (auto loc = einfo.errPos->getCodeLines()) {
            oss << "\n";
            printCodeLines(oss, "", *einfo.errPos, *loc);
            oss << "\n";
        } else
            oss << noSource;
    }

    auto suggestions = einfo.suggestions.trim();
    if (! suggestions.suggestions.empty()){
        oss << "Did you mean " <<
            suggestions.trim() <<
            "?" << std::endl;
    }

    // traces
    if (showTrace && !einfo.traces.empty()) {
        for (auto iter = einfo.traces.rbegin(); iter != einfo.traces.rend(); ++iter) {
            oss << "\n" << "… " << iter->hint.str() << "\n";

            if (iter->pos) {
                oss << "\n" << ANSI_BLUE << "at " ANSI_WARNING << *iter->pos << ANSI_NORMAL << ":";

                if (auto loc = iter->pos->getCodeLines()) {
                    oss << "\n";
                    printCodeLines(oss, "", *iter->pos, *loc);
                    oss << "\n";
                } else
                    oss << noSource;
            }
        }
    }

    out << indent(prefix, std::string(filterANSIEscapes(prefix, true).size(), ' '), chomp(oss.str()));

    return out;
}
}

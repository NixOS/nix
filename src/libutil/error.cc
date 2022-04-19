#include "error.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"
#include <sstream>

namespace nix {

const std::string nativeSystem = SYSTEM;

void BaseError::addTrace(std::optional<ErrPos> e, hintformat hint)
{
    err.traces.push_front(Trace { .pos = e, .hint = hint });
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

std::ostream & operator<<(std::ostream & os, const hintformat & hf)
{
    return os << hf.str();
}

std::string showErrPos(const ErrPos & errPos)
{
    if (errPos.line > 0) {
        if (errPos.column > 0) {
            return fmt("%d:%d", errPos.line, errPos.column);
        } else {
            return fmt("%d", errPos.line);
        }
    }
    else {
        return "";
    }
}

std::optional<LinesOfCode> getCodeLines(const ErrPos & errPos)
{
    if (errPos.line <= 0)
        return std::nullopt;

    if (errPos.origin == foFile) {
        LinesOfCode loc;
        try {
            // FIXME: when running as the daemon, make sure we don't
            // open a file to which the client doesn't have access.
            AutoCloseFD fd = open(errPos.file.c_str(), O_RDONLY | O_CLOEXEC);
            if (!fd) return {};

            // count the newlines.
            int count = 0;
            std::string line;
            int pl = errPos.line - 1;
            do
            {
                line = readLine(fd.get());
                ++count;
                if (count < pl)
                    ;
                else if (count == pl)
                    loc.prevLineOfCode = line;
                else if (count == pl + 1)
                    loc.errLineOfCode = line;
                else if (count == pl + 2) {
                    loc.nextLineOfCode = line;
                    break;
                }
            } while (true);
            return loc;
        }
        catch (EndOfFile & eof) {
            if (loc.errLineOfCode.has_value())
                return loc;
            else
                return std::nullopt;
        }
        catch (std::exception & e) {
            return std::nullopt;
        }
    } else {
        std::istringstream iss(errPos.file);
        // count the newlines.
        int count = 0;
        std::string line;
        int pl = errPos.line - 1;

        LinesOfCode loc;

        do
        {
            std::getline(iss, line);
            ++count;
            if (count < pl)
            {
                ;
            }
            else if (count == pl) {
                loc.prevLineOfCode = line;
            } else if (count == pl + 1) {
                loc.errLineOfCode = line;
            } else if (count == pl + 2) {
                loc.nextLineOfCode = line;
                break;
            }

            if (!iss.good())
                break;
        } while (true);

        return loc;
    }
}

// print lines of code to the ostream, indicating the error column.
void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const ErrPos & errPos,
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

void printAtPos(const ErrPos & pos, std::ostream & out)
{
    if (pos) {
        switch (pos.origin) {
            case foFile: {
                out << fmt(ANSI_BLUE "at " ANSI_WARNING "%s:%s" ANSI_NORMAL ":", pos.file, showErrPos(pos));
                break;
            }
            case foString: {
                out << fmt(ANSI_BLUE "at " ANSI_WARNING "«string»:%s" ANSI_NORMAL ":", showErrPos(pos));
                break;
            }
            case foStdin: {
                out << fmt(ANSI_BLUE "at " ANSI_WARNING "«stdin»:%s" ANSI_NORMAL ":", showErrPos(pos));
                break;
            }
            default:
                throw Error("invalid FileOrigin in errPos");
        }
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

    if (einfo.errPos.has_value() && *einfo.errPos) {
        oss << "\n";
        printAtPos(*einfo.errPos, oss);

        auto loc = getCodeLines(*einfo.errPos);

        // lines of code.
        if (loc.has_value()) {
            oss << "\n";
            printCodeLines(oss, "", *einfo.errPos, *loc);
            oss << "\n";
        }
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

            if (iter->pos.has_value() && (*iter->pos)) {
                auto pos = iter->pos.value();
                oss << "\n";
                printAtPos(pos, oss);

                auto loc = getCodeLines(pos);
                if (loc.has_value()) {
                    oss << "\n";
                    printCodeLines(oss, "", pos, *loc);
                    oss << "\n";
                }
            }
        }
    }

    out << indent(prefix, std::string(filterANSIEscapes(prefix, true).size(), ' '), chomp(oss.str()));

    return out;
}
}

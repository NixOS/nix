#include "error.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"
#include <sstream>

namespace nix {

const std::string nativeSystem = SYSTEM;

BaseError & BaseError::addTrace(std::optional<ErrPos> e, hintformat hint)
{
    err.traces.push_front(Trace { .pos = e, .hint = hint });
    return *this;
}

// c++ std::exception descendants must have a 'const char* what()' function.
// This stringifies the error and caches it for use by what(), or similarly by msg().
const string & BaseError::calcWhat() const
{
    if (what_.has_value())
        return *what_;
    else {
        err.name = sname();

        std::ostringstream oss;
        showErrorInfo(oss, err, false);
        what_ = oss.str();

        return *what_;
    }
}

std::optional<string> ErrorInfo::programName = std::nullopt;

std::ostream & operator<<(std::ostream & os, const hintformat & hf)
{
    return os << hf.str();
}

string showErrPos(const ErrPos & errPos)
{
    if (errPos.line > 0) {
        if (errPos.column > 0) {
            return fmt("(%1%:%2%)", errPos.line, errPos.column);
        } else {
            return fmt("(%1%)", errPos.line);
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
            AutoCloseFD fd = open(errPos.file.c_str(), O_RDONLY | O_CLOEXEC);
            if (!fd) {
                logError(SysError("opening file '%1%'", errPos.file).info());
                return std::nullopt;
            }
            else
            {
                // count the newlines.
                int count = 0;
                string line;
                int pl = errPos.line - 1;
                do
                {
                    line = readLine(fd.get());
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
                } while (true);
                return loc;
            }
        }
        catch (EndOfFile & eof) {
            if (loc.errLineOfCode.has_value())
                return loc;
            else
                return std::nullopt;
        }
        catch (std::exception & e) {
            printError("error reading nix file: %s\n%s", errPos.file, e.what());
            return std::nullopt;
        }
    } else {
        std::istringstream iss(errPos.file);
        // count the newlines.
        int count = 0;
        string line;
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
    const string & prefix,
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

void printAtPos(const string & prefix, const ErrPos & pos, std::ostream & out)
{
    if (pos)
    {
        switch (pos.origin) {
            case foFile: {
                out << prefix << ANSI_BLUE << "at: " << ANSI_YELLOW << showErrPos(pos) <<
                    ANSI_BLUE << " in file: " << ANSI_NORMAL << pos.file;
                break;
            }
            case foString: {
                out << prefix << ANSI_BLUE << "at: " << ANSI_YELLOW << showErrPos(pos) <<
                    ANSI_BLUE << " from string" << ANSI_NORMAL;
                break;
            }
            case foStdin: {
                out << prefix << ANSI_BLUE << "at: " << ANSI_YELLOW << showErrPos(pos) <<
                    ANSI_BLUE << " from stdin" << ANSI_NORMAL;
                break;
            }
            default:
                throw Error("invalid FileOrigin in errPos");
        }
    }
}

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace)
{
    auto errwidth = std::max<size_t>(getWindowSize().second, 20);
    string prefix = "";

    string levelString;
    switch (einfo.level) {
        case Verbosity::lvlError: {
            levelString = ANSI_RED;
            levelString += "error:";
            levelString += ANSI_NORMAL;
            break;
        }
        case Verbosity::lvlWarn: {
            levelString = ANSI_YELLOW;
            levelString += "warning:";
            levelString += ANSI_NORMAL;
            break;
        }
        case Verbosity::lvlInfo: {
            levelString = ANSI_GREEN;
            levelString += "info:";
            levelString += ANSI_NORMAL;
            break;
        }
        case Verbosity::lvlTalkative: {
            levelString = ANSI_GREEN;
            levelString += "talk:";
            levelString += ANSI_NORMAL;
            break;
        }
        case Verbosity::lvlChatty: {
            levelString = ANSI_GREEN;
            levelString += "chat:";
            levelString += ANSI_NORMAL;
            break;
        }
        case Verbosity::lvlVomit: {
            levelString = ANSI_GREEN;
            levelString += "vomit:";
            levelString += ANSI_NORMAL;
            break;
        }
        case Verbosity::lvlDebug: {
            levelString = ANSI_YELLOW;
            levelString += "debug:";
            levelString += ANSI_NORMAL;
            break;
        }
        default: {
            levelString = fmt("invalid error level: %1%", einfo.level);
            break;
        }
    }

    auto ndl = prefix.length()
        + filterANSIEscapes(levelString, true).length()
        + 7
        + einfo.name.length()
        + einfo.programName.value_or("").length();
    auto dashwidth = std::max<int>(errwidth - ndl, 3);

    std::string dashes(dashwidth, '-');

    // divider.
    if (einfo.name != "")
        out << fmt("%1%%2%" ANSI_BLUE " --- %3% %4% %5%" ANSI_NORMAL,
            prefix,
            levelString,
            einfo.name,
            dashes,
            einfo.programName.value_or(""));
    else
        out << fmt("%1%%2%" ANSI_BLUE " -----%3% %4%" ANSI_NORMAL,
            prefix,
            levelString,
            dashes,
            einfo.programName.value_or(""));

    bool nl = false;  // intersperse newline between sections.
    if (einfo.errPos.has_value() && (*einfo.errPos)) {
        out << prefix << std::endl;
        printAtPos(prefix, *einfo.errPos, out);
        nl = true;
    }

    // description
    if (einfo.description != "") {
        if (nl)
            out << std::endl << prefix;
        out << std::endl << prefix << einfo.description;
        nl = true;
    }

    if (einfo.errPos.has_value() && (*einfo.errPos)) {
        auto loc = getCodeLines(*einfo.errPos);

        // lines of code.
        if (loc.has_value()) {
            if (nl)
                out << std::endl << prefix;
            printCodeLines(out, prefix, *einfo.errPos, *loc);
            nl = true;
        }
    }

    // hint
    if (einfo.hint.has_value()) {
        if (nl)
            out << std::endl << prefix;
        out << std::endl << prefix << *einfo.hint;
        nl = true;
    }

    // traces
    if (showTrace && !einfo.traces.empty())
    {
        const string tracetitle(" show-trace ");

        int fill = errwidth - tracetitle.length();
        int lw = 0;
        int rw = 0;
        const int min_dashes = 3;
        if (fill > min_dashes * 2) {
            if (fill % 2 != 0) {
                lw = fill / 2;
                rw = lw + 1;
            }
            else
            {
                lw = rw = fill / 2;
            }
        }
        else
            lw = rw = min_dashes;

        if (nl)
            out << std::endl << prefix;

        out << ANSI_BLUE << std::string(lw, '-') << tracetitle << std::string(rw, '-') << ANSI_NORMAL;

        for (auto iter = einfo.traces.rbegin(); iter != einfo.traces.rend(); ++iter)
        {
            out << std::endl << prefix;
            out << ANSI_BLUE << "trace: " << ANSI_NORMAL << iter->hint.str();

            if (iter->pos.has_value() && (*iter->pos)) {
                auto pos = iter->pos.value();
                out << std::endl << prefix;
                printAtPos(prefix, pos, out);

                auto loc = getCodeLines(pos);
                if (loc.has_value())
                {
                    out << std::endl << prefix;
                    printCodeLines(out, prefix, pos, *loc);
                    out << std::endl << prefix;
                }
            }
        }
    }

    return out;
}
}

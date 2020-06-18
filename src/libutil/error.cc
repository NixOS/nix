#include "error.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"
#include <sstream>

namespace nix {


const std::string nativeSystem = SYSTEM;

// addPrefix is used for show-trace.  Strings added with addPrefix
// will print ahead of the error itself.
BaseError & BaseError::addPrefix(const FormatOrString & fs)
{
    prefix_ = fs.s + prefix_;
    return *this;
}

// c++ std::exception descendants must have a 'const char* what()' function.
// This stringifies the error and caches it for use by what(), or similarly by msg().
const string& BaseError::calcWhat() const
{
    if (what_.has_value())
        return *what_;
    else {
        err.name = sname();

        std::ostringstream oss;
        oss << err;
        what_ = oss.str();

        return *what_;
    }
}

std::optional<string> ErrorInfo::programName = std::nullopt;

std::ostream& operator<<(std::ostream &os, const hintformat &hf)
{
    return os << hf.str();
}

string showErrPos(const ErrPos &errPos)
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

// if nixCode contains lines of code, print them to the ostream, indicating the error column.
void printCodeLines(std::ostream &out, const string &prefix, const NixCode &nixCode)
{
    // previous line of code.
    if (nixCode.prevLineOfCode.has_value()) {
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
                prefix,
                (nixCode.errPos.line - 1),
                *nixCode.prevLineOfCode);
    }

    if (nixCode.errLineOfCode.has_value()) {
        // line of code containing the error.
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
                prefix,
                (nixCode.errPos.line),
                *nixCode.errLineOfCode);
        // error arrows for the column range.
        if (nixCode.errPos.column > 0) {
            int start = nixCode.errPos.column;
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
    if (nixCode.nextLineOfCode.has_value()) {
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
                prefix,
                (nixCode.errPos.line + 1),
                *nixCode.nextLineOfCode);
    }
}

std::ostream& operator<<(std::ostream &out, const ErrorInfo &einfo)
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

    auto ndl = prefix.length() + levelString.length() + 3 + einfo.name.length() + einfo.programName.value_or("").length();
    auto dashwidth = ndl > (errwidth - 3) ? 3 : errwidth - ndl;

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
    if (einfo.nixCode.has_value()) {
        if (einfo.nixCode->errPos.file != "") {
            // filename, line, column.
            out << std::endl << fmt("%1%in file: " ANSI_BLUE "%2% %3%" ANSI_NORMAL,
                prefix,
                einfo.nixCode->errPos.file,
                showErrPos(einfo.nixCode->errPos));
        } else {
            out << std::endl << fmt("%1%from command line argument", prefix);
        }
        nl = true;
    }

    // description
    if (einfo.description != "") {
        if (nl)
            out << std::endl << prefix;
        out << std::endl << prefix << einfo.description;
        nl = true;
    }

    // lines of code.
    if (einfo.nixCode.has_value() && einfo.nixCode->errLineOfCode.has_value()) {
        if (nl)
            out << std::endl << prefix;
        printCodeLines(out, prefix, *einfo.nixCode);
        nl = true;
    }

    // hint
    if (einfo.hint.has_value()) {
        if (nl)
            out << std::endl << prefix;
        out << std::endl << prefix << *einfo.hint;
        nl = true;
    }

    return out;
}
}

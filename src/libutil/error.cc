#include "types.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"

namespace nix
{


const std::string nativeSystem = SYSTEM;


BaseError & BaseError::addPrefix(const FormatOrString & fs)
{
    prefix_ = fs.s + prefix_;
    return *this;
}


std::string SysError::addErrno(const std::string & s)
{
    errNo = errno;
    return s + ": " + strerror(errNo);
}


std::optional<string> ErrorInfo::programName = std::nullopt;

std::ostream& operator<<(std::ostream &os, const hintformat &hf)
{
    return os << hf.str();
}

string showErrPos(const ErrPos &errPos)
{
    if (errPos.column > 0) {
        return fmt("(%1%:%2%)", errPos.line, errPos.column);
    } else {
        return fmt("(%1%)", errPos.line);
    };
}

void printCodeLines(const string &prefix, const NixCode &nixCode)
{
    // previous line of code.
    if (nixCode.prevLineOfCode.has_value()) {
        std::cout << fmt("%1% %|2$5d|| %3%",
                         prefix,
                         (nixCode.errPos.line - 1),
                         *nixCode.prevLineOfCode)
                  << std::endl;
    }

    // line of code containing the error.%2$+5d%
    std::cout << fmt("%1% %|2$5d|| %3%",
                     prefix,
                     (nixCode.errPos.line),
                     nixCode.errLineOfCode)
              << std::endl;

    // error arrows for the column range.
    if (nixCode.errPos.column > 0) {
        int start = nixCode.errPos.column;
        std::string spaces;
        for (int i = 0; i < start; ++i) {
            spaces.append(" ");
        }

        std::string arrows("^");

        std::cout << fmt("%1%      |%2%" ANSI_RED "%3%" ANSI_NORMAL,
                         prefix,
                         spaces,
                         arrows) << std::endl;
    }

    // next line of code.
    if (nixCode.nextLineOfCode.has_value()) {
        std::cout << fmt("%1% %|2$5d|| %3%",
                         prefix,
                         (nixCode.errPos.line + 1),
                         *nixCode.nextLineOfCode)
                  << std::endl;
    }
}

std::ostream& operator<<(std::ostream &out, const ErrorInfo &einfo)
{
    int errwidth = 80;
    string prefix = "    ";

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

    int ndl = prefix.length() + levelString.length() + 3 + einfo.name.length() + einfo.programName.value_or("").length();
    int dashwidth = ndl > (errwidth - 3) ? 3 : errwidth - ndl;

    string dashes;
    for (int i = 0; i < dashwidth; ++i)
        dashes.append("-");

    // divider.
    out << fmt("%1%%2%" ANSI_BLUE " %3% %4% %5% %6%" ANSI_NORMAL,
               prefix,
               levelString,
               "---",
               einfo.name,
               dashes,
               einfo.programName.value_or(""))
        << std::endl;

    // filename.
    if (einfo.nixCode.has_value()) {
        if (einfo.nixCode->errPos.file != "") {
            string eline = einfo.nixCode->errLineOfCode != ""
                           ? string(" ") + showErrPos(einfo.nixCode->errPos)
                           : "";

            out << fmt("%1%in file: " ANSI_BLUE "%2%%3%" ANSI_NORMAL,
                       prefix,
                       einfo.nixCode->errPos.file,
                       eline) << std::endl;
            out << prefix << std::endl;
        } else {
            out << fmt("%1%from command line argument", prefix) << std::endl;
            out << prefix << std::endl;
        }
    }

    // description
    out << prefix << einfo.description << std::endl;
    out << prefix << std::endl;

    // lines of code.
    if (einfo.nixCode.has_value() && einfo.nixCode->errLineOfCode != "") {
        printCodeLines(prefix, *einfo.nixCode);
        out << prefix << std::endl;
    }

    // hint
    if (einfo.hint.has_value()) {
        out << prefix << *einfo.hint << std::endl;
        out << prefix << std::endl;
    }

    return out;
}
}

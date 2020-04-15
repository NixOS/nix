#include "error.hh"

#include <iostream>
#include <optional>

namespace nix
{

std::optional<string> ErrorInfo::programName = std::nullopt;

std::ostream& operator<<(std::ostream &os, const hintformat &hf)
{
    return os << hf.str();
}

string showErrPos(const ErrPos &errPos)
{
    if (errPos.column > 0) {
        return fmt("(%1%:%2%)", errPos.lineNumber, errPos.column);
    } else {
        return fmt("(%1%)", errPos.lineNumber);
    };
}

void printCodeLines(const string &prefix, const NixCode &nixCode)
{
    // previous line of code.
    if (nixCode.prevLineOfCode.has_value()) {
        std::cout << fmt("%1% %|2$5d|| %3%",
                         prefix,
                         (nixCode.errPos.lineNumber - 1),
                         *nixCode.prevLineOfCode)
                  << std::endl;
    }

    // line of code containing the error.%2$+5d%
    std::cout << fmt("%1% %|2$5d|| %3%",
                     prefix,
                     (nixCode.errPos.lineNumber),
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
                         (nixCode.errPos.lineNumber + 1),
                         *nixCode.nextLineOfCode)
                  << std::endl;
    }
}

void printErrorInfo(const ErrorInfo &einfo)
{
    int errwidth = 80;
    string prefix = "    ";

    string levelString;
    switch (einfo.level) {
    case ErrLevel::elError: {
        levelString = ANSI_RED;
        levelString += "error:";
        levelString += ANSI_NORMAL;
        break;
    }
    case ErrLevel::elWarning: {
        levelString = ANSI_YELLOW;
        levelString += "warning:";
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
    std::cout << fmt("%1%%2%" ANSI_BLUE " %3% %4% %5% %6%" ANSI_NORMAL,
                     prefix,
                     levelString,
                     "---",
                     einfo.name,
                     dashes,
                     einfo.programName.value_or(""))
              << std::endl;

    // filename.
    if (einfo.nixCode.has_value()) {
        if (einfo.nixCode->errPos.nixFile != "") {
            string eline = einfo.nixCode->errLineOfCode != ""
                           ? string(" ") + showErrPos(einfo.nixCode->errPos)
                           : "";

            std::cout << fmt("%1%in file: " ANSI_BLUE "%2%%3%" ANSI_NORMAL,
                             prefix, 
                             einfo.nixCode->errPos.nixFile,
                             eline) << std::endl;
            std::cout << prefix << std::endl;
        } else {
            std::cout << fmt("%1%from command line argument", prefix) << std::endl;
            std::cout << prefix << std::endl;
        }
    }

    // description
    std::cout << prefix << einfo.description << std::endl;
    std::cout << prefix << std::endl;

    // lines of code.
    if (einfo.nixCode->errLineOfCode != "") {
        printCodeLines(prefix, *einfo.nixCode);
        std::cout << prefix << std::endl;
    }

    // hint
    if (einfo.hint.has_value()) {
        std::cout << prefix << *einfo.hint << std::endl;
        std::cout << prefix << std::endl;
    }
}

}

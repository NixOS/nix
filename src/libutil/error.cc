#include "error.hh"

#include <iostream>
#include <optional>

namespace nix
{

std::optional<string> ErrorInfo::programName = std::nullopt;

string showErrLine(ErrLine &errLine)
{
    if (errLine.column > 0) {
        return fmt("(%1%:%2%)", errLine.lineNumber, errLine.column);
    } else {
        return fmt("(%1%)", errLine.lineNumber);
    };
}

void printCodeLines(string &prefix, NixCode &nixCode)
{

    if (nixCode.errLine.has_value()) {
        // previous line of code.
        if (nixCode.errLine->prevLineOfCode.has_value()) {
            std::cout << fmt("%1% %|2$5d|| %3%",
                             prefix,
                             (nixCode.errLine->lineNumber - 1),
                             *nixCode.errLine->prevLineOfCode)
                      << std::endl;
        }

        // line of code containing the error.%2$+5d%
        std::cout << fmt("%1% %|2$5d|| %3%",
                         prefix,
                         (nixCode.errLine->lineNumber),
                         nixCode.errLine->errLineOfCode)
                  << std::endl;

        // error arrows for the column range.
        if (nixCode.errLine->column > 0) {
            int start = nixCode.errLine->column;
            std::string spaces;
            for (int i = 0; i < start; ++i) {
                spaces.append(" ");
            }

            // for now, length of 1.
            std::string arrows("^");

            std::cout << fmt("%1%      |%2%" ANSI_RED "%3%" ANSI_NORMAL,
                             prefix,
                             spaces,
                             arrows) << std::endl;
        }



        // next line of code.
        if (nixCode.errLine->nextLineOfCode.has_value()) {
            std::cout << fmt("%1% %|2$5d|| %3%",
                             prefix,
                             (nixCode.errLine->lineNumber + 1),
                             *nixCode.errLine->nextLineOfCode)
                      << std::endl;
        }

    }

}

void printErrorInfo(ErrorInfo &einfo)
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
    std::cout << fmt("%1%%2%" ANSI_BLUE " %3% %4% %5% %6%" ANSI_NORMAL
                     , prefix
                     , levelString
                     , "---"
                     , einfo.name
                     , dashes
                     , einfo.programName.value_or(""))
              << std::endl;

    // filename.
    if (einfo.nixCode.has_value()) {
        if (einfo.nixCode->nixFile.has_value()) {
            string eline = einfo.nixCode->errLine.has_value()
                           ? string(" ") + showErrLine(*einfo.nixCode->errLine)
                           : "";

            std::cout << fmt("%1%in file: " ANSI_BLUE "%2%%3%" ANSI_NORMAL
                             , prefix, *einfo.nixCode->nixFile, eline) << std::endl;
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
    if (einfo.nixCode.has_value()) {
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

#include "logging.hh"
#include "nixexpr.hh"

#include <iostream>
#include <optional>

using namespace nix;

MakeError(DemoError, Error);

int main()
{
    makeDefaultLogger();

    verbosity = lvlVomit;

    // In each program where errors occur, this has to be set.
    ErrorInfo::programName = std::optional("error-demo");

    try {
        throw DemoError("demo error was thrown");
    } catch (Error &e) {
        logger->logEI(e.info());
    }


    // ErrorInfo constructor
    try {
        auto e = Error("generic error");
        throw DemoError(e.info());
    } catch (Error &e) {
        logger->logEI(e.info());
    }


    // For completeness sake, info through vomit levels.
    // But this is maybe a heavy format for those.
    logger->logEI(
        ErrorInfo { .level = lvlInfo,
                    .name = "Info name",
                    .description = "Info description",
        });

    logger->logEI(
        ErrorInfo { .level = lvlTalkative,
                    .name = "Talkative name",
                    .description = "Talkative description",
        });

    logger->logEI(
        ErrorInfo { .level = lvlChatty,
                    .name = "Chatty name",
                    .description = "Chatty description",
        });

    logger->logEI(
        ErrorInfo { .level = lvlDebug,
                    .name = "Debug name",
                    .description = "Debug description",
        });

    logger->logEI(
        ErrorInfo { .level = lvlVomit,
                    .name = "Vomit name",
                    .description = "Vomit description",
        });

    // Error in a program; no hint and no nix code.
    logError(
        ErrorInfo { .name = "name",
                    .description = "error description",
        });

    // Warning with name, description, and hint.
    // The hintfmt function makes all the substituted text yellow.
    logWarning(
        ErrorInfo { .name = "name",
                    .description = "error description",
                    .hint = hintfmt("there was a %1%", "warning"),
        });

    // Warning with nix file, line number, column, and the lines of
    // code where a warning occurred.
    SymbolTable testTable;
    auto problem_file = testTable.create("myfile.nix");

    logWarning(
        ErrorInfo { .name = "warning name",
                    .description = "warning description",
                    .hint = hintfmt("this hint has %1% templated %2%!!",
                        "yellow",
                        "values"),
                    .nixCode = NixCode {
                        .errPos = Pos(problem_file, 40, 13),
                        .prevLineOfCode = std::nullopt,
                        .errLineOfCode = "this is the problem line of code",
                        .nextLineOfCode = std::nullopt
                    }});

    // Error with previous and next lines of code.
    logError(
        ErrorInfo { .name = "error name",
                    .description = "error description",
                    .hint = hintfmt("this hint has %1% templated %2%!!",
                        "yellow",
                        "values"),
                    .nixCode = NixCode {
                        .errPos = Pos(problem_file, 40, 13),
                        .prevLineOfCode = std::optional("previous line of code"),
                        .errLineOfCode = "this is the problem line of code",
                        .nextLineOfCode = std::optional("next line of code"),
                    }});


    return 0;
}
